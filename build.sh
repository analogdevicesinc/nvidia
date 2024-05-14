#!/bin/bash

KERNEL_OUT_PATH="../../kernel_out"
MODULES_PATH="../../modules_out"
SOURCE_PATH="."

DEFCONFIG_TARGET="tegra_defconfig"
IMAGE_TARGET="Image"
MODULES_TARGET="modules"
MODULES_INSTALL_TARGET="modules_install"
DTBS_TARGET="dtbs"

KERNEL_LOCALVERSION="-tegra"

TARGETS=()
TARGETS+=("$DEFCONFIG_TARGET")
TARGETS+=("$IMAGE_TARGET")
TARGETS+=("$DTBS_TARGET")

O_OPT=()
O_OPT+=(-C "${SOURCE_PATH}")
O_OPT+=(ARCH="arm64")
O_OPT+=(LOCALVERSION="$KERNEL_LOCALVERSION")
O_OPT+=(O="${KERNEL_OUT_PATH}")
O_OPT+=(CROSS_COMPILE="$CROSS_COMPILE")
O_OPT+=(-j$(nproc))

echo "Targets: ${TARGETS[@]}"
echo "Options: ${O_OPT[@]}"

make "${O_OPT[@]}" "${TARGETS[@]}"
if [[ $? -ne 0 ]]; then
	exit
fi

NV_DISPLAY_PATH="../../tegra/kernel-src/nv-kernel-display-driver/NVIDIA-kernel-module-source-TempVersion"

KERNEL_SRC_PATH_ABS=$(realpath "$SOURCE_PATH")
KERNEL_OUT_PATH_ABS=$(realpath "$KERNEL_OUT_PATH")

NV_DISPLAY_O_OPT=(
	"SYSSRC=$KERNEL_SRC_PATH_ABS"
	"SYSOUT=$KERNEL_OUT_PATH_ABS"
	"CC=${CROSS_COMPILE}gcc"
	"LD=${CROSS_COMPILE}ld"
	"AR=${CROSS_COMPILE}ar"
	"CXX=${CROSS_COMPILE}g++"
	"OBJCOPY=${CROSS_COMPILE}objcopy"
	"TARGET_ARCH=aarch64"
)

build_modules() {
	local dir_path="$1"
	shift

	local opts=("$@")

	if [[ -n "$dir_path" ]]; then
		pushd "$dir_path"
		if [[ $? -ne 0 ]]; then
			return $?
		fi
	fi

	make "${O_OPT[@]}" "${opts[@]}" "$MODULES_TARGET"
	if [[ $? -ne 0 ]]; then
		return $?
	fi

	if [[ -n "$dir_path" ]]; then
		popd
	fi
}

build_nv_display_modules() {
	if [[ ! -d "$NV_DISPLAY_PATH" ]]; then
		return 0
	fi

	build_modules "$NV_DISPLAY_PATH" "${NV_DISPLAY_O_OPT[@]}"
}

install_modules() {
	local modules_path="$1"
	shift

	local clean_dir="$1"
	shift

	local dir_path="$1"
	shift

	local opts=("$@")

	local mod_path_arg
	local mod_strip_arg

	if [[ -n "$modules_path" ]]; then
		if [[ -n "$clean_dir" ]]; then
			rm -rf "$modules_path"
		fi
		mkdir -p "$modules_path"
		modules_path_abs=$(realpath "$modules_path")
		mod_path_arg="INSTALL_MOD_PATH=${modules_path_abs}"
		mod_strip_arg="INSTALL_MOD_STRIP=1"
	fi

	if [[ -n "$dir_path" ]]; then
		pushd "$dir_path"
		if [[ $? -ne 0 ]]; then
			return $?
		fi
	fi

	make "${O_OPT[@]}" "${opts[@]}" "$mod_path_arg" "$mod_strip_arg" "$MODULES_INSTALL_TARGET"

	if [[ -n "$dir_path" ]]; then
		popd
	fi
}

install_kernel_modules() {
	local modules_path="$1"
	shift

	install_modules "$modules_path" 1
}

install_nv_display_modules() {
	if [[ ! -d "$NV_DISPLAY_PATH" ]]; then
		return 0
	fi

	local modules_path="$1"
	shift

	# To get the same output as Nvidia does when installing the display
	# module, set INSTALL_MOD_DIR line in kernel-open/Makefile to
	# KBUILD_PARAMS += INSTALL_MOD_DIR=extra/opensrc-disp
	install_modules "$modules_path" "" "$NV_DISPLAY_PATH" "${NV_DISPLAY_O_OPT[@]}"
}

build_modules
if [[ $? -ne 0 ]]; then
	exit
fi

build_nv_display_modules
if [[ $? -ne 0 ]]; then
	exit
fi

install_kernel_modules "$MODULES_PATH"
if [[ $? -ne 0 ]]; then
	exit
fi

install_nv_display_modules "$MODULES_PATH"
if [[ $? -ne 0 ]]; then
	exit
fi
