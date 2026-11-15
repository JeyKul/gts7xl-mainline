#!/bin/bash
# Turn this project's kernel .config into the recovery one. Usage: kernel-config.sh <linux src> <build dir>
# The build dir starts as a copy of the Linux build's .config. Android's init needs
# SELinux and binder; TWRP's ramdisk comes from the image, not a built-in initramfs;
# the bootloader's cmdline is ignored (CMDLINE_FORCE), so the androidboot.* values TWRP
# reads are built in, with mainline's device names.
set -euo pipefail
S=$(realpath "$1"); B=$(realpath "$2")
# audit=0: permissive SELinux would flood the kernel log with denials
C="console=null quiet vt.global_cursor_default=0 audit=0 log_buf_len=4M arm-smmu.disable_bypass=0 panic=30"
C="$C msm.separate_gpu_kms=1 msm.r8q_zap_dyn=1 msm.r8q_zap_secvid=0 fw_devlink.sync_state=timeout"
C="$C firmware_class.path=/tmp/gts7xl-fw androidboot.hardware=qcom androidboot.memcg=1"
C="$C androidboot.selinux=permissive printk.devkmsg=on loop.max_part=7 buildvariant=eng"
C="$C androidboot.usbcontroller=a600000.usb androidboot.boot_devices=soc@0/1d84000.ufshc androidboot.bootdevice=1d84000.ufshc"
cd "$S"
scripts/config --file "$B/.config" --set-str INITRAMFS_SOURCE "" --set-str CMDLINE "$C" -e CMDLINE_FORCE \
	-e SECURITY_NETWORK -e SECURITY_SELINUX -e SECURITY_SELINUX_DEVELOP -e AUDIT -e DEFAULT_SECURITY_SELINUX \
	--set-str LSM "landlock,lockdown,yama,loadpin,safesetid,selinux,ipe,bpf" \
	-e ANDROID_BINDER_IPC -e ANDROID_BINDERFS --set-str ANDROID_BINDER_DEVICES "binder,hwbinder,vndbinder" \
	-e FUSE_FS -e BTRFS_FS -e DM_CRYPT \
	-e QCOM_SPMI_ADC5 -e TIMED_VIBRATOR -e RTC_DRV_PM8XXX -e INPUT_UINPUT \
	-e CFG80211 -e MAC80211 -e RFKILL -e QRTR -e QRTR_MHI -e QRTR_SMD -e MHI_BUS -e QCOM_QMI_HELPERS \
	-e ATH_COMMON -e ATH11K -e ATH11K_PCI -e POWER_SEQUENCING_QCOM_WCN -e PCI_PWRCTRL_PWRSEQ \
	-e CRYPTO_CCM -e CRYPTO_GCM -e CRYPTO_LIB_ARC4
ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make -s O="$B" olddefconfig
