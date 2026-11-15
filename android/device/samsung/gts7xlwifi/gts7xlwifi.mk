# Galaxy Tab S7+ Wi-Fi (SM-T970) on the mainline kernel
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit_only.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/updatable_apex.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/emulated_storage.mk)
$(call inherit-product, frameworks/native/build/tablet-10in-xhdpi-2048-dalvik-heap.mk)
# Display through DRM/KMS, GPU through Mesa freedreno, buffers through minigbm:
# the same open stack as AOSP's own Qualcomm boards on near-mainline kernels
$(call inherit-product, device/linaro/dragonboard/shared/graphics/drm_hwcomposer/device.mk)
$(call inherit-product, device/samsung/gts7xlwifi/mesa.mk)
$(call inherit-product, device/linaro/dragonboard/shared/graphics/minigbm_msm/device.mk)
$(call inherit-product, device/samsung/gts7xlwifi/device.mk)

PRODUCT_NAME := gts7xlwifi
PRODUCT_DEVICE := gts7xlwifi
PRODUCT_BRAND := samsung
PRODUCT_MODEL := SM-T970
PRODUCT_MANUFACTURER := samsung
