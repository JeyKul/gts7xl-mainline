// SPDX-License-Identifier: GPL-2.0
/* gts7xl test: boot the IPA firmware (GSI + uC) through TrustZone like stock PIL does. */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/firmware.h>
#include <linux/firmware/qcom/qcom_scm.h>
#include <linux/firmware/qcom/qcom_pas.h>
#include <linux/interconnect.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/soc/qcom/mdt_loader.h>
#include <linux/soc/qcom/qcom_aoss.h>

static int hold_ms = 2000;
static bool use_tzmem = true;
static char *fw;
module_param(fw, charp, 0444);
static uint pas;
module_param(pas, uint, 0444);
static ulong base, size;
module_param(base, ulong, 0444);
module_param(size, ulong, 0444);
module_param(use_tzmem, bool, 0444);
module_param(hold_ms, int, 0444);
static char *msg;
module_param(msg, charp, 0444);

static int boot_one(struct device *dev, int idx, const char *name, u32 pas)
{
	struct reserved_mem *rmem;
	struct device_node *np;
	const struct firmware *fw;
	char path[64];
	void *va;
	int ret;

	np = of_parse_phandle(dev->of_node, "memory-region", idx);
	rmem = np ? of_reserved_mem_lookup(np) : NULL;
	of_node_put(np);
	if (!rmem)
		return -ENODEV;

	dev_info(dev, "%s: pas %u supported %d, shutdown first %d\n", name, pas,
		 qcom_scm_pas_supported(pas), qcom_scm_pas_shutdown(pas));
	snprintf(path, sizeof(path), "qcom/sm8250/gts7xl/%s.mdt", name);
	ret = request_firmware(&fw, path, dev);
	if (ret)
		return ret;

	va = memremap(rmem->base, rmem->size, MEMREMAP_WC);
	if (!va) {
		release_firmware(fw);
		return -ENOMEM;
	}
	if (use_tzmem) {
		struct qcom_pas_context *ctx = devm_qcom_pas_context_alloc(dev, pas, rmem->base, rmem->size);

		if (IS_ERR(ctx)) {
			ret = PTR_ERR(ctx);
		} else {
			ctx->use_tzmem = true;
			ret = qcom_mdt_pas_load(ctx, fw, path, NULL);
		}
	} else {
		ret = qcom_mdt_load(dev, fw, path, pas, va, rmem->base, rmem->size, NULL);
	}
	memunmap(va);
	release_firmware(fw);
	if (ret) {
		dev_err(dev, "%s: mdt load %d\n", name, ret);
		return ret;
	}
	ret = qcom_scm_pas_auth_and_reset(pas);
	dev_info(dev, "%s (pas %u) at %pa: auth_and_reset %d\n", name, pas, &rmem->base, ret);
	return ret;
}

static int boot_generic(struct device *dev)
{
	const struct firmware *f;
	char path[64];
	void *va;
	int ret;

	snprintf(path, sizeof(path), "qcom/sm8250/gts7xl/%s.mdt", fw);
	ret = request_firmware(&f, path, dev);
	if (ret)
		return ret;
	va = memremap(base, size, MEMREMAP_WC);
	if (!va) {
		release_firmware(f);
		return -ENOMEM;
	}
	ret = qcom_mdt_load(dev, f, path, pas, va, base, size, NULL);
	memunmap(va);
	release_firmware(f);
	if (!ret)
		ret = qcom_scm_pas_auth_and_reset(pas);
	dev_info(dev, "generic %s pas %u at 0x%lx: %d\n", fw, pas, base, ret);
	return ret;
}

static int loader_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct icc_path *mem, *cfg;
	struct clk *core;
	int r1, r2;

	if (!qcom_scm_is_available())
		return -EPROBE_DEFER;
	if (msg) {
		struct qmp *q = qmp_get(dev);
		int r = -ENODEV;

		if (IS_ERR(q))
			return PTR_ERR(q);
		r = qmp_send(q, "%s", msg);
		qmp_put(q);
		dev_info(dev, "qmp \"%s\": %d\n", msg, r);
		return 0;
	}
	core = devm_clk_get(dev, "core");
	if (IS_ERR(core))
		return PTR_ERR(core);
	mem = devm_of_icc_get(dev, "memory");
	cfg = devm_of_icc_get(dev, "config");
	if (IS_ERR(mem) || IS_ERR(cfg))
		return -EPROBE_DEFER;

	{
		static const u32 ids[] = { 1, 4, 12, 15, 18, 23, 24, 25, 26, 27, 30 };
		int i;

		for (i = 0; i < ARRAY_SIZE(ids); i++)
			dev_info(dev, "pas %u supported %d\n", ids[i], qcom_scm_pas_supported(ids[i]));
	}
	clk_prepare_enable(core);
	icc_set_bw(mem, 0, 600000);
	icc_set_bw(cfg, 0, 76800);

	if (fw) {
		r1 = boot_generic(dev);
		r2 = 0;
	} else {
		r1 = boot_one(dev, 0, "ipa_fws", 15);
		r2 = boot_one(dev, 1, "ipa_uc", 27);
	}

	msleep(hold_ms);
	icc_set_bw(cfg, 0, 0);
	icc_set_bw(mem, 0, 0);
	clk_disable_unprepare(core);
	{
		struct qmp *qmp = qmp_get(dev);
		int q = -ENODEV;

		if (!IS_ERR(qmp)) {
			q = qmp_send(qmp, "{class: bcm, res: ipa_pc, val: 1}");
			qmp_put(qmp);
		}
		dev_info(dev, "ipa_pc 1 to AOP: %d\n", q);
	}
	dev_info(dev, "done: fws %d uc %d, votes released\n", r1, r2);
	return 0;
}

static const struct of_device_id loader_match[] = {
	{ .compatible = "gts7xl,ipa-loader" },
	{ }
};
MODULE_DEVICE_TABLE(of, loader_match);

static struct platform_driver loader_driver = {
	.probe = loader_probe,
	.driver = { .name = "gts7xl-ipa-loader", .of_match_table = loader_match },
};
module_platform_driver(loader_driver);
MODULE_DESCRIPTION("gts7xl test: boot IPA firmware through TrustZone");
MODULE_LICENSE("GPL");
