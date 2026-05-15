#!/bin/bash

SCRIPT_DIR="$(dirname "$(realpath "$0")")"

. "$SCRIPT_DIR/common.sh"

print_usage() {
	echo "usage: $0 <linux-for-tegra-dir> <repo-dir> <remote-name> <project> <tag> [options]"
	echo "example: $0 ../Linux_for_Tegra_R35.3.1 ../Linux_for_Tegra_R35.3.1/sources/kernel/kernel-5.10 adi gmsl jetson_35.3.1"
	echo "options:"
	echo "-f|--force: force-push"
}

POSITIONAL_ARGS=()

while [[ $# -gt 0 ]]; do
	case $1 in
	-f|--force)
		FORCE=1
		shift
		;;
	-*|--*)
		echo "Unknown option $1"
		print_usage
		exit 1
		;;
	*)
		POSITIONAL_ARGS+=("$1")
		shift
		;;
	esac
done

set -- "${POSITIONAL_ARGS[@]}"

if [[ $# -ne 5 ]]; then
	print_usage
	exit 1
fi

L4T_PATH="$1"
REPO_PATH="$2"
REMOTE_NAME="$3"
PROJECT="$4"
TAG_NAME="$5"

L4T_PATH_ABS=$(realpath "$L4T_PATH/sources")

REPO_PATH_ABS=$(realpath "$REPO_PATH")
REPO_PATH_REL=$(rel_path "$L4T_PATH_ABS" "$REPO_PATH")

if ! starts_with "$L4T_PATH_ABS" "$REPO_PATH_ABS"; then
	echo "Repo path is not relative to Linux for Tegra path"
	exit 1
fi

BRANCH_NAME=$(compose_branch_name "$PROJECT" "$TAG_NAME" "$REPO_PATH_REL")

push_dir "$REPO_PATH"

push_head_to_branch "$REMOTE_NAME" "$BRANCH_NAME" "$FORCE"

pop_dir
