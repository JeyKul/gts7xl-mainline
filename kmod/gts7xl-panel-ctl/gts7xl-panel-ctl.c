// SPDX-License-Identifier: GPL-2.0-only
/*
 * Galaxy Tab S7+ panel control while the bootloader's display pipeline keeps
 * running. Sends panel commands over the DSI link it left enabled, which gives
 * real brightness and screen off on top of the firmware framebuffer.
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/platform_device.h>

#define DSI_BASE		0x0ae94000
#define DSI_SIZE		0x400
#define DSI_CTRL		0x004
#define DSI_DMA_BASE		0x048
#define DSI_DMA_LEN		0x04c
#define DSI_TRIG_DMA		0x090
#define DSI_INTR_CTRL		0x110
#define DSI_INTR_DMA_DONE	BIT(0)
#define DSI_CTRL_EXPECTED	0x1f5

#define MAX_BRIGHTNESS		3544 /* Top of the stock normal range, 0xdd8 */
#define DMA_BUF_SIZE		64

static struct platform_device *ctl_pdev;

struct panel_ctl {
	void __iomem *dsi;
	u8 *buf;
	dma_addr_t buf_dma;
	struct mutex lock;
	bool off;
};

/* Same in-memory format as the msm driver: stock sends everything as 0x39 */
static int panel_ctl_send(struct panel_ctl *ctl, const u8 *payload, size_t size)
{
	size_t len = ALIGN(size + 4, 4);
	u32 status;
	int ret;

	ctl->buf[0] = size & 0xff;
	ctl->buf[1] = size >> 8;
	ctl->buf[2] = 0x39;
	ctl->buf[3] = BIT(7) | BIT(6); /* Last packet, long */
	memcpy(ctl->buf + 4, payload, size);
	memset(ctl->buf + 4 + size, 0xff, len - size - 4);
	dma_wmb();

	writel(readl(ctl->dsi + DSI_INTR_CTRL), ctl->dsi + DSI_INTR_CTRL);
	writel(lower_32_bits(ctl->buf_dma), ctl->dsi + DSI_DMA_BASE);
	writel(len, ctl->dsi + DSI_DMA_LEN);
	writel(1, ctl->dsi + DSI_TRIG_DMA);

	ret = readl_poll_timeout(ctl->dsi + DSI_INTR_CTRL, status,
				 status & DSI_INTR_DMA_DONE, 100, 100000);
	writel(status, ctl->dsi + DSI_INTR_CTRL);

	return ret;
}

#define SEND(ctl, ...) ({						\
	static const u8 d[] = { __VA_ARGS__ };				\
	panel_ctl_send(ctl, d, sizeof(d));				\
})

static int panel_ctl_update(struct backlight_device *bl)
{
	struct panel_ctl *ctl = bl_get_data(bl);
	int level = backlight_get_brightness(bl);
	u8 cmd[3] = { 0x51, level & 0xff, level >> 8 };
	int ret;

	mutex_lock(&ctl->lock);

	if (!level) {
		ret = ctl->off ? 0 : SEND(ctl, 0x28);
		ctl->off = true;
		goto out;
	}

	if (ctl->off) {
		ret = SEND(ctl, 0x29);
		if (ret)
			goto out;
		ctl->off = false;
	}

	/* Stock gamma_mode2_normal: 51h takes the low byte first */
	ret = SEND(ctl, 0x68, 0x19, 0x20, 0x00, 0x14);
	if (!ret)
		ret = panel_ctl_send(ctl, cmd, sizeof(cmd));
	if (!ret)
		ret = SEND(ctl, 0x53, 0x20);
out:
	mutex_unlock(&ctl->lock);

	return ret;
}

static const struct backlight_ops panel_ctl_ops = {
	.update_status = panel_ctl_update,
};

static int panel_ctl_probe(struct platform_device *pdev)
{
	struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
		.max_brightness = MAX_BRIGHTNESS,
		.brightness = 800,
	};
	struct device *dev = &pdev->dev;
	struct backlight_device *bl;
	struct panel_ctl *ctl;
	u32 val;
	int ret;

	ctl = devm_kzalloc(dev, sizeof(*ctl), GFP_KERNEL);
	if (!ctl)
		return -ENOMEM;
	mutex_init(&ctl->lock);

	ctl->dsi = devm_ioremap(dev, DSI_BASE, DSI_SIZE);
	if (!ctl->dsi)
		return -ENOMEM;

	val = readl(ctl->dsi + DSI_CTRL);
	if (val != DSI_CTRL_EXPECTED)
		return dev_err_probe(dev, -ENODEV, "unexpected DSI state 0x%x\n", val);

	ret = dma_coerce_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret)
		return ret;

	ctl->buf = dmam_alloc_coherent(dev, DMA_BUF_SIZE, &ctl->buf_dma, GFP_KERNEL);
	if (!ctl->buf)
		return -ENOMEM;

	bl = devm_backlight_device_register(dev, "gts7xl-panel", dev, ctl,
					    &panel_ctl_ops, &props);
	if (IS_ERR(bl))
		return PTR_ERR(bl);

	dev_info(dev, "ready, command buffer at %pad\n", &ctl->buf_dma);

	return 0;
}

static struct platform_driver panel_ctl_driver = {
	.probe = panel_ctl_probe,
	.driver = {
		.name = "gts7xl-panel-ctl",
	},
};

static int __init panel_ctl_init(void)
{
	struct device_node *np;
	int ret;

	/* The native panel driver owns the link on images that enable it */
	np = of_find_compatible_node(NULL, NULL, "samsung,s6tuum3");
	if (of_device_is_available(np)) {
		of_node_put(np);
		return -ENODEV;
	}
	of_node_put(np);

	ret = platform_driver_register(&panel_ctl_driver);
	if (ret)
		return ret;

	ctl_pdev = platform_device_register_simple("gts7xl-panel-ctl", -1, NULL, 0);
	if (IS_ERR(ctl_pdev)) {
		platform_driver_unregister(&panel_ctl_driver);
		return PTR_ERR(ctl_pdev);
	}

	return 0;
}
module_init(panel_ctl_init);

static void __exit panel_ctl_exit(void)
{
	platform_device_unregister(ctl_pdev);
	platform_driver_unregister(&panel_ctl_driver);
}
module_exit(panel_ctl_exit);

MODULE_DESCRIPTION("Galaxy Tab S7+ panel control over the firmware DSI link");
MODULE_LICENSE("GPL");
