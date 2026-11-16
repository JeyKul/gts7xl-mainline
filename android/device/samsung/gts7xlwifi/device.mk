LOCAL_PATH := device/samsung/gts7xlwifi

PRODUCT_SHIPPING_API_LEVEL := 37
PRODUCT_USE_DYNAMIC_PARTITIONS := true
PRODUCT_BUILD_SUPER_PARTITION := true
PRODUCT_USE_DYNAMIC_PARTITION_SIZE := true
AB_OTA_UPDATER := false
PRODUCT_CHARACTERISTICS := tablet
PRODUCT_AAPT_CONFIG := normal large xlarge
PRODUCT_AAPT_PREF_CONFIG := xhdpi
DEVICE_PACKAGE_OVERLAYS += $(LOCAL_PATH)/overlay
PRODUCT_RUNTIMES := runtime_libart_default
PRODUCT_OTA_ENFORCE_VINTF_KERNEL_REQUIREMENTS := false

# Kernel (prepare-kernel.sh fills prebuilt/)
PRODUCT_COPY_FILES += $(LOCAL_PATH)/prebuilt/Image.gz-dtb:kernel

# Partitions, init, first stage
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/fstab.gts7xlwifi:$(TARGET_COPY_OUT_RAMDISK)/fstab.gts7xlwifi \
    $(LOCAL_PATH)/fstab.gts7xlwifi:$(TARGET_COPY_OUT_VENDOR)/etc/fstab.gts7xlwifi \
    $(LOCAL_PATH)/init.gts7xlwifi.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.gts7xlwifi.rc \
    $(LOCAL_PATH)/init.gts7xlwifi.usb.rc:$(TARGET_COPY_OUT_VENDOR)/etc/init/hw/init.gts7xlwifi.usb.rc \
    $(LOCAL_PATH)/ueventd.gts7xlwifi.rc:$(TARGET_COPY_OUT_VENDOR)/etc/ueventd.rc \
    frameworks/base/data/keyboards/Generic.kl:$(TARGET_COPY_OUT_VENDOR)/usr/keylayout/gts7xlwifi.kl

# No ashmem on mainline: memfd (found out with TWRP on this kernel).
# drm_hwcomposer scans card0, card1, ... and stops at the first gap: the
# display is card1 (card0 is the boot framebuffer's, gone once msm takes over)
PRODUCT_VENDOR_PROPERTIES += \
    sys.use_memfd=true \
    ro.soc.manufacturer=Qualcomm \
    ro.soc.model=SM8250 \
    persist.sys.zram_enabled=1 \
    ro.sf.lcd_density=320 \
    vendor.hwc.drm.device=/dev/dri/card1

# Security without TrustZone access
PRODUCT_PACKAGES += \
    com.android.hardware.keymint.rust_nonsecure \
    com.android.hardware.gatekeeper.nonsecure

# Wi-Fi (ath11k over nl80211), as on AOSP's Qualcomm dev boards
PRODUCT_PACKAGES += hostapd libwpa_client wpa_supplicant wificond
PRODUCT_PROPERTY_OVERRIDES += wifi.interface=wlan0 wifi.supplicant_scan_interval=15
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.wifi.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.wifi.xml \
    device/linaro/dragonboard/wpa_supplicant_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant_overlay.conf \
    device/linaro/dragonboard/p2p_supplicant_overlay.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/p2p_supplicant_overlay.conf \
    $(LOCAL_PATH)/wpa_supplicant.conf:$(TARGET_COPY_OUT_VENDOR)/etc/wifi/wpa_supplicant.conf

# Bluetooth (QCA6390 on a UART, HCI through the kernel)
PRODUCT_PACKAGES += android.hardware.bluetooth-service.default
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.bluetooth.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth.xml \
    frameworks/native/data/etc/android.hardware.bluetooth_le.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.bluetooth_le.xml

# Audio: AOSP's default AIDL audio HAL (Android 17 wants the AIDL core) with
# LineageOS mainline's handheld policy; the speakers' own config comes later
PRODUCT_PACKAGES += \
    com.android.hardware.audio \
    tinyplay tinycap tinymix tinypcminfo
PRODUCT_COPY_FILES += \
    hardware/interfaces/audio/aidl/default/audio_effects_config.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects_config.xml \
    $(LOCAL_PATH)/audio/audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/default_volume_tables.xml:$(TARGET_COPY_OUT_VENDOR)/etc/default_volume_tables.xml \
    frameworks/av/services/audiopolicy/config/primary_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/primary_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/usb_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/usb_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/r_submix_audio_policy_configuration.xml:$(TARGET_COPY_OUT_VENDOR)/etc/r_submix_audio_policy_configuration.xml \
    frameworks/av/services/audiopolicy/config/audio_policy_volumes.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_policy_volumes.xml \
    frameworks/av/services/audiopolicy/config/bluetooth_audio_policy_configuration_7_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/bluetooth_audio_policy_configuration_7_0.xml \
    device/linaro/dragonboard/etc/media_codecs.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_video.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_video.xml \
    frameworks/av/media/libstagefright/data/media_codecs_google_audio.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_google_audio.xml \
    device/linaro/dragonboard/seccomp_policy/mediaswcodec.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/mediaswcodec.policy

# Health, power, thermal, USB: AOSP's reference services read sysfs
PRODUCT_PACKAGES += \
    android.hardware.health-service.example \
    android.hardware.power-service.example \
    com.android.hardware.thermal \
    android.hardware.usb-service.example \
    suspend_blocker

# Qualcomm remote processors (ADSP for audio/sensors): service locator and file server
PRODUCT_PACKAGES += pd-mapper qrtr-ns qrtr-cfg qrtr-lookup tqftpserv

# Firmware: links into the tablet's own apnhlos (only the amp blobs in proprietary/ are in the tree)
# plus redistributable linux-firmware files (prepare-kernel.sh fetches them)
PRODUCT_PACKAGES += gts7xl-firmware-links gts7xl-btaddr
PRODUCT_COPY_FILES += $(LOCAL_PATH)/gts7xl-dsp-up.sh:$(TARGET_COPY_OUT_VENDOR)/bin/gts7xl-dsp-up.sh
# The mount point itself, under etc (the build only creates files there):
# vendor is read only, and init can't create it later
PRODUCT_COPY_FILES += $(LOCAL_PATH)/apnhlos/README:$(TARGET_COPY_OUT_VENDOR)/etc/apnhlos/README
PRODUCT_COPY_FILES += \
    $(call find-copy-subdir-files,*,$(LOCAL_PATH)/prebuilt/firmware,$(TARGET_COPY_OUT_VENDOR)/firmware)
# Speaker amp protection firmware and calibration (see proprietary/README)
PRODUCT_COPY_FILES += \
    $(call find-copy-subdir-files,*,$(LOCAL_PATH)/proprietary/firmware,$(TARGET_COPY_OUT_VENDOR)/firmware)

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/tablet_core_hardware.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/tablet_core_hardware.xml \
    frameworks/native/data/etc/android.hardware.touchscreen.multitouch.jazzhand.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.touchscreen.multitouch.jazzhand.xml \
    frameworks/native/data/etc/android.hardware.usb.accessory.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.accessory.xml \
    frameworks/native/data/etc/android.hardware.usb.host.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.usb.host.xml \
    frameworks/native/data/etc/android.software.app_widgets.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.app_widgets.xml \
    frameworks/native/data/etc/android.software.backup.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.backup.xml

PRODUCT_PACKAGES += Launcher3QuickStep
PRODUCT_SOONG_NAMESPACES += device/samsung/gts7xlwifi device/linaro/dragonboard

# Codec2 over AIDL: the default (HIDL) needs hwservicemanager, which this
# device doesn't run, and leaves only the in-process AAC decoder
PRODUCT_PRODUCT_PROPERTIES += media.c2.hal.selection=aidl

# Twice the default HAL buffer (~260 ms): AudioFlinger writes 4096-frame
# chunks, the default 4x1440 buffer underran on any scheduling delay
PRODUCT_VENDOR_PROPERTIES += ro.hardware.audio.tinyalsa.period_size_multiplier=2
