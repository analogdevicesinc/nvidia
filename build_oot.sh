#!/bin/bash

SOURCE_PATH="."
KERNEL_PATH="$SOURCE_PATH/kernel"
KERNEL_PATH_ABS=$(realpath "$KERNEL_PATH")
KERNEL_SRC_PATH="$KERNEL_PATH/kernel-jammy-src"

KERNEL_HEADERS_PATH="$KERNEL_SRC_PATH"
KERNEL_HEADERS_PATH_ABS=$(realpath "$KERNEL_HEADERS_PATH")

MODULES_OUT_PATH="$SOURCE_PATH/modules_out"
MODULES_OUT_PATH_ABS=$(realpath "$MODULES_OUT_PATH")

KERNEL_OUT_PATH="$SOURCE_PATH/kernel_out"

KERNEL_IMAGE_PATH="$KERNEL_SRC_PATH/arch/arm64/boot/Image"
KERNEL_IMAGE_OUT_PATH="$KERNEL_OUT_PATH/arch/arm64/boot"

KERNEL_DTB_PATH="$SOURCE_PATH/nvidia-oot/device-tree/platform/generic-dts/dtbs"
KERNEL_DTB_OUT_PATH="$KERNEL_OUT_PATH/arch/arm64/boot/dts/nvidia"

mkdir -p "$MODULES_OUT_PATH"
mkdir -p "$MODULES_OUT_PATH/boot"
mkdir -p "$KERNEL_OUT_PATH"
mkdir -p "$KERNEL_IMAGE_OUT_PATH"
mkdir -p "$KERNEL_DTB_OUT_PATH"

MODULES_TARGET="modules"
MODULES_INSTALL_TARGET="modules_install"
DTBS_TARGET="dtbs"

O_OPT=()
O_OPT+=(INSTALL_MOD_PATH="$MODULES_OUT_PATH_ABS")
O_OPT+=(KERNEL_HEADERS="$KERNEL_HEADERS_PATH_ABS")

make -C "$KERNEL_PATH"
if [[ $? -ne 0 ]]; then
	exit
fi

make "${O_OPT[@]}" modules
if [[ $? -ne 0 ]]; then
	exit
fi

make "${O_OPT[@]}" dtbs
if [[ $? -ne 0 ]]; then
	exit
fi

make "${O_OPT[@]}" install -C "$KERNEL_PATH_ABS"
if [[ $? -ne 0 ]]; then
	exit
fi

make "${O_OPT[@]}" modules_install
if [[ $? -ne 0 ]]; then
	exit
fi

cp "$KERNEL_DTB_PATH"/* "$KERNEL_DTB_OUT_PATH"
cp "$KERNEL_IMAGE_PATH" "$KERNEL_IMAGE_OUT_PATH"
