#!/bin/bash

SCRIPT_DIR="$(dirname "$(realpath "$0")")"

. "$SCRIPT_DIR/common.sh"

print_usage() {
	echo "usage: $0 <linux-for-tegra-dir> <patches-dir>"
	echo "example: $0 ../Linux_for_Tegra_R35.3.1 ./patches"
}

if [[ $# -ne 2 ]]; then
	print_usage
	exit 1
fi

L4T_PATH="$1"
PATCHES_PATH="$2"

L4T_PATH_ABS=$(realpath "$L4T_PATH/sources")
PATCHES_PATH_ABS=$(realpath "$PATCHES_PATH")

PATCH_DIR_PATHS=$(find_patch_dirs "$PATCHES_PATH_ABS")

while read -r PATCH_DIR_PATH
do
	PATCHES_PATH_REL=$(rel_path "$PATCHES_PATH_ABS" "$PATCH_DIR_PATH")
	REPO_PATH="$L4T_PATH_ABS/$PATCHES_PATH_REL"

	PATCHES=$(find_patches "$PATCH_DIR_PATH")
	PATCH_COUNT=$(count_lines "$PATCHES")

	echo "Applying to $PATCHES_PATH_REL"
	echo "Found $PATCH_COUNT commits"

	init_repo "$REPO_PATH"

	push_dir "$REPO_PATH"

	while read -r PATCH
	do
		apply_patch "$PATCH"
	done <<< "$PATCHES"

	pop_dir
done <<< "$PATCH_DIR_PATHS"

echo "$PATCH_DIRS"
