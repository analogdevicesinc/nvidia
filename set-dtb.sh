#!/bin/bash

print_usage() {
	echo "usage: $0 <dtb> <ip>"
}

if [[ $# -lt 2 ]]; then
	print_usage
	exit 1
fi

DTB="$1"
IP="$2"

cmd() {
	ssh "root@$IP" "$1"
}

DTB_PATH="/boot/dtb/kernel_$DTB"

cmd "sed -i -r 's#^(      FDT )(.+)\$#\1$DTB_PATH#g' /boot/extlinux/extlinux.conf"
