#!/bin/bash

SCRIPT_DIR="$(dirname "$(realpath "$0")")"

. "$SCRIPT_DIR/common.sh"

print_usage() {
	echo "usage: $0 <linux-for-tegra-dir> <base-remote-name> [project] <base-tag> <output-dir>"
	echo "example: $0 ../Linux_for_Tegra_R35.3.1 origin jetson_35.3.1 ./patches"
	echo "example: $0 ../Linux_for_Tegra_R35.3.1 adi gmsl jetson_35.3.1 ./patches"
}

if [[ $# -ne 4 ]] && [[ $# -ne 5 ]]; then
	print_usage
	exit 1
fi

L4T_PATH="$1"
REMOTE_NAME="$2"

if [[ $# -eq 4 ]]; then
	TAG_NAME="$3"
	OUTPUT_PATH="$4"
elif [[ $# -eq 5 ]]; then
	PROJECT="$3"
	TAG_NAME="$4"
	OUTPUT_PATH="$5"
else
	print_usage
	exit 1
fi

OUTPUT_PATH="$4"

L4T_PATH_ABS=$(realpath "$L4T_PATH/sources")
OUTPUT_PATH_ABS=$(realpath "$OUTPUT_PATH")

REPO_PATHS=$(find_repos "$L4T_PATH_ABS")

rm -rf "$OUTPUT_PATH"
mkdir -p "$OUTPUT_PATH"

generate_patches() {
	START="$1"
	PATCHES_OUTPUT_PATH="$2"

	if [[ -n "$START" ]]; then
		RANGE="$START..HEAD"
	else
		RANGE="HEAD"
		ROOT=1
	fi

	PATCH_COUNT=$(count_patches "$RANGE")

	echo "Found $PATCH_COUNT commits"
	if [[ "$PATCH_COUNT" -eq 0 ]]; then
		return
	fi

	mkdir -p "$PATCHES_OUTPUT_PATH"
	PATCHES=$(format_patches_to_output "$RANGE" "$ROOT" "$PATCHES_OUTPUT_PATH")
}

while read -r REPO_PATH
do
	REPO_PATH_REL=$(rel_path "$L4T_PATH_ABS" "$REPO_PATH")
	PATCHES_OUTPUT_PATH="$OUTPUT_PATH_ABS/$REPO_PATH_REL"

	echo "Extracting from $REPO_PATH_REL"

	push_dir "$REPO_PATH"

	if [[ -n "$PROJECT" ]]; then
		add_remote "$REMOTE_NAME" "$REMOTE_URL"
		BRANCH_NAME=$(compose_branch_name "$PROJECT" "$TAG_NAME" "$REPO_PATH_REL")
	else
		BRANCH_NAME="$TAG_NAME"
	fi

	fetch_remote_ref "$REMOTE_NAME" "$BRANCH_NAME"
	if [[ $? -ne 0 ]]; then
		echo "$REMOTE_NAME/$BRANCH_NAME does not exist, generating new repo patches"
		generate_patches "" "$PATCHES_OUTPUT_PATH"
	else
		generate_patches "FETCH_HEAD" "$PATCHES_OUTPUT_PATH"
	fi

	pop_dir
done <<< "$REPO_PATHS"
