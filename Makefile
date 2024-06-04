old-dtb := $(dtb-y)
old-dtbo := $(dtbo-y)
dtb-y :=
dtbo-y :=
makefile-path := platform/t19x/gmsl522/kernel-dts

BUILD_ENABLE=n
ifneq ($(filter y,$(CONFIG_ARCH_TEGRA_19x_SOC) $(CONFIG_ARCH_TEGRA_194_SOC)),)
BUILD_ENABLE=y
endif

dtb-$(BUILD_ENABLE) += tegra194-p3668-0001-gmsl522-reva-gmsl-0.dtb
dtb-$(BUILD_ENABLE) += tegra194-p3668-0001-gmsl522-reva-gmsl-1.dtb
dtb-$(BUILD_ENABLE) += tegra194-p3668-0001-gmsl522-reva-gmsl-2.dtb
dtb-$(BUILD_ENABLE) += tegra194-p3668-0001-gmsl522-reva-gmsl-3.dtb
dtb-$(BUILD_ENABLE) += tegra194-p3668-0001-gmsl522-revb-gmsl-0.dtb
dtb-$(BUILD_ENABLE) += tegra194-p3668-0001-gmsl522-revb-gmsl-1.dtb
dtb-$(BUILD_ENABLE) += tegra194-p3668-0001-gmsl522-revb-gmsl-2.dtb
dtb-$(BUILD_ENABLE) += tegra194-p3668-0001-gmsl522-revb-gmsl-3.dtb

ifneq ($(dtb-y),)
dtb-y := $(addprefix $(makefile-path)/,$(dtb-y))
endif
ifneq ($(dtbo-y),)
dtbo-y := $(addprefix $(makefile-path)/,$(dtbo-y))
endif

dtb-y += $(old-dtb)
dtbo-y += $(old-dtbo)
