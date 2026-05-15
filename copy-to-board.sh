#!/bin/bash

DTB_PREFIX=kernel_
KERNEL_OUT_PATH="../../kernel_out"
MODULES_OUT_PATH="../../modules_out"
KERNEL_VERSION_PATH="$KERNEL_OUT_PATH/include/config/kernel.release"
KERNEL_SRC="$KERNEL_OUT_PATH/arch/arm64/boot/Image"
DTB_SRC="$KERNEL_OUT_PATH/arch/arm64/boot/dts/nvidia"

KERNEL_TARGET="/boot"
DTB_TARGET="/boot/dtb"

print_usage() {
	echo "usage: $0 <ip>"
}

if [[ $# -lt 1 ]]; then
	print_usage
	exit 1
fi

IP="$1"

rsync_transfer() {
	SRC="$1"
	TARGET="$2"
	rsync -av --checksum --omit-dir-times --delete "$SRC" "root@$IP":"$TARGET"
}

cmd() {
	ssh "root@$IP" "$1"
}

cp_dtbs() {
	DTB_SRC_TMP=$(mktemp -d)

	find "$DTB_SRC" -maxdepth 1 -type f -name "*.dtb" -exec cp {} "$DTB_SRC_TMP" \;

	DTBS=$(find "$DTB_SRC_TMP" -type f)

	while read DTB; do
		DTB_NAME=$(basename "$DTB")
		mv "$DTB" "$DTB_SRC_TMP/$DTB_PREFIX$DTB_NAME"
	done <<< "$DTBS"

	rsync_transfer "$DTB_SRC_TMP/" "$DTB_TARGET"

	rm -rf "$DTB_SRC_TMP"
}

cp_kernel() {
	rsync_transfer "$KERNEL_SRC" "$KERNEL_TARGET"
}

cp_modules() {
	KERNEL_VERSION=$(cat "$KERNEL_VERSION_PATH")
	rsync_transfer "$MODULES_OUT_PATH/lib/modules/$KERNEL_VERSION" "/lib/modules/"
}

cp_kernel
cp_dtbs
cp_modules

echo "Syncing..."
cmd sync

echo "Reboot?"
select yn in "Yes" "No"; do
	case $yn in
		Yes)
			echo "Rebooting..."
			cmd reboot
			exit
			;;
		No)
			exit
			;;
	esac
done
