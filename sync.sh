#!/bin/bash

SCRIPT_DIR="$(dirname "$(realpath "$0")")"

. "$SCRIPT_DIR/common.sh"

print_usage() {
	echo "usage: $0 <linux-for-tegra-dir> <remote-name> [<remote-url> <project>] <tag>"
	echo "example: $0 ../Linux_for_Tegra_R35.3.1 origin jetson_35.3.1"
	echo "example: $0 ../Linux_for_Tegra_R35.3.1 adi https://github.com/analogdevicesinc/nvidia.git gmsl jetson_35.3.1"
}

if [[ $# -ne 3 ]] && [[ $# -ne 5 ]]; then
	print_usage
	exit 1
fi

L4T_PATH="$1"
REMOTE_NAME="$2"

if [[ $# -eq 3 ]]; then
	TAG_NAME="$3"
elif [[ $# -eq 5 ]]; then
	REMOTE_URL="$3"
	PROJECT="$4"
	TAG_NAME="$5"
else
	print_usage
	exit 1
fi

L4T_PATH_ABS=$(realpath "$L4T_PATH/sources")

if [[ -n "$PROJECT" ]]; then
	REPO_PATHS=$(find_monorepo_paths "$L4T_PATH_ABS" "$REMOTE_URL" "$PROJECT" "$TAG_NAME")
else
	REPO_PATHS=$(find_repos "$L4T_PATH_ABS")
fi

while read -r REPO_PATH
do
	# rel_path requires the path to actually exist, and this is not true for
	# monorepos which have new paths
	init_repo "$REPO_PATH"

	REPO_PATH_REL=$(rel_path "$L4T_PATH_ABS" "$REPO_PATH")

	echo "Syncing $REPO_PATH_REL"

	push_dir "$REPO_PATH"

	if [[ -n "$PROJECT" ]]; then
		add_remote "$REMOTE_NAME" "$REMOTE_URL"
		BRANCH_NAME=$(compose_branch_name "$PROJECT" "$TAG_NAME" "$REPO_PATH_REL")
	else
		BRANCH_NAME="$TAG_NAME"
	fi

	fetch_remote_ref "$REMOTE_NAME" "$BRANCH_NAME"
	if [[ $? -ne 0 ]]; then
		echo "$REMOTE_NAME/$BRANCH_NAME does not exist, removing directory"

		pop_dir

		rm -rf "$REPO_PATH"
	else
		reset_to_fetched_ref

		pop_dir
	fi
done <<< "$REPO_PATHS"
