# Mesa freedreno for the Adreno 650, as device/linaro/dragonboard/shared/graphics/mesa
# but without its dev-board properties (density 160 for monitors, rotation lock)
PRODUCT_PACKAGES += \
    libGLES_mesa libEGL_mesa libGLESv1_CM_mesa libGLESv2_mesa libgallium_dri libglapi \
    vulkan.freedreno
TARGET_BUILD_MESA := true
PRODUCT_SOONG_NAMESPACES += external/mesa3d
PRODUCT_PROPERTY_OVERRIDES += \
    ro.hardware.egl=mesa \
    ro.opengles.version=196608
PRODUCT_VENDOR_PROPERTIES += \
    ro.hardware.vulkan=freedreno \
    debug.hwui.renderer=skiagl
TARGET_USES_VULKAN := true
PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.opengles.aep.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.opengles.aep.xml \
    frameworks/native/data/etc/android.software.opengles.deqp.level-2022-03-01.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.opengles.deqp.level.xml \
    frameworks/native/data/etc/android.hardware.vulkan.compute-0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.compute.xml \
    frameworks/native/data/etc/android.hardware.vulkan.level-1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.level.xml \
    frameworks/native/data/etc/android.hardware.vulkan.version-1_1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.vulkan.version.xml \
    frameworks/native/data/etc/android.software.vulkan.deqp.level-2021-03-01.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.software.vulkan.deqp.level.xml
