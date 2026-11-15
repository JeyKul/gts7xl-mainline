# Galaxy Tab S7+ Wi-Fi: SM8250, Samsung bootloader (no vendor_boot, boot header
# v2 at most, no A/B), this project's mainline kernel. Recovery stays TWRP.
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-2a-dotprod
TARGET_CPU_VARIANT := cortex-a76
TARGET_CPU_ABI := arm64-v8a
TARGET_BOOTLOADER_BOARD_NAME := gts7xlwifi
TARGET_BOARD_PLATFORM := kona
TARGET_NO_BOOTLOADER := true
TARGET_NO_RECOVERY := true

# Kernel: Image.gz with the DTB appended (prepare-kernel.sh), cmdline built in.
# The same layout the Linux boot image uses on this bootloader.
TARGET_NO_KERNEL := false
BOARD_BOOT_HEADER_VERSION := 1
BOARD_KERNEL_PAGESIZE := 2048
BOARD_KERNEL_BASE := 0x10000000
BOARD_MKBOOTIMG_ARGS := --header_version 1 --kernel_offset 0x00008000 --ramdisk_offset 0x01000000 --tags_offset 0x00000100
BOARD_RAMDISK_USE_LZ4 := false

BOARD_BOOTIMAGE_PARTITION_SIZE := 71303168
BOARD_USERDATAIMAGE_PARTITION_SIZE := 242986500096
BOARD_USERDATAIMAGE_FILE_SYSTEM_TYPE := f2fs
BOARD_CACHEIMAGE_PARTITION_SIZE := 419430400
BOARD_CACHEIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_FLASH_BLOCK_SIZE := 262144
# The system image's /metadata mount point (first stage moves the mount there)
BOARD_USES_METADATA_PARTITION := true

# super: the tablet's 10171187200 bytes, one slot, metadata from the start
BOARD_SUPER_PARTITION_SIZE := 10171187200
BOARD_SUPER_PARTITION_GROUPS := gts7xl_dynamic_partitions
BOARD_GTS7XL_DYNAMIC_PARTITIONS_SIZE := 10166992896
BOARD_GTS7XL_DYNAMIC_PARTITIONS_PARTITION_LIST := system system_ext product vendor vendor_dlkm
BOARD_SUPER_PARTITION_METADATA_DEVICE := super
BOARD_EXT4_SHARE_DUP_BLOCKS := true
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := erofs
BOARD_SYSTEM_EXTIMAGE_FILE_SYSTEM_TYPE := erofs
BOARD_PRODUCTIMAGE_FILE_SYSTEM_TYPE := erofs
BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE := erofs
BOARD_USES_VENDOR_DLKMIMAGE := true
BOARD_VENDOR_DLKMIMAGE_FILE_SYSTEM_TYPE := erofs
TARGET_COPY_OUT_SYSTEM_EXT := system_ext
TARGET_COPY_OUT_PRODUCT := product
TARGET_COPY_OUT_VENDOR := vendor
TARGET_COPY_OUT_VENDOR_DLKM := vendor_dlkm

# Kernel modules from the mainline build, all in vendor_dlkm. ueventd loads them
# by device alias as they appear (like udev on Linux); these few are loaded at
# boot, as Linux's modules-load.d does
BOARD_VENDOR_KERNEL_MODULES := $(wildcard device/samsung/gts7xlwifi/prebuilt/modules/*.ko)
BOARD_VENDOR_KERNEL_MODULES_LOAD := hci_uart.ko rtc-pm8xxx.ko qcom-spmi-adc5.ko uinput.ko

# Graphics
include device/linaro/dragonboard/shared/graphics/drm_hwcomposer/BoardConfig.mk
# Mesa from external/mesa (LineageOS's, upstream with Android's meson glue):
# AOSP 17's external/mesa3d only builds the emulator drivers
BOARD_MESA3D_USES_MESON_BUILD := true
BOARD_MESA3D_GALLIUM_DRIVERS := freedreno
BOARD_MESA3D_VULKAN_DRIVERS := freedreno
# freedreno needs no OpenCL compiler (mesa-clc auto), and no SPIRV-Tools
BOARD_MESA3D_MESON_ARGS += -Dspirv-tools=disabled
# Linaro's minigbm policy, but labeling only the vendor copy of libdrm (Treble)
BOARD_SEPOLICY_DIRS += device/samsung/gts7xlwifi/sepolicy/minigbm

# Software KeyMint (no TrustZone access): FBE works, without hardware protection
BOARD_SEPOLICY_DIRS += device/samsung/gts7xlwifi/sepolicy
DEVICE_MANIFEST_FILE := device/samsung/gts7xlwifi/manifest.xml
BOARD_VNDK_VERSION := current
BUILD_BROKEN_DUP_RULES := true
BUILD_BROKEN_ELF_PREBUILT_PRODUCT_COPY_FILES := true
