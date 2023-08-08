ANALOG_NVIDIA_REMOTE_URL="https://github.com/analogdevicesinc/nvidia.git"

push_dir() {
	pushd "$@" > /dev/null
}

pop_dir() {
	popd "$@" > /dev/null
}

starts_with() { case $2 in "$1"*) true;; *) false;; esac; }

rel_path() {
	TO_PATH="$1"
	FROM_PATH="$2"

	realpath --relative-to="$TO_PATH" "$FROM_PATH"
}

find_repos() {
	find "$1" -type d -name ".git" | xargs dirname | sort
}

find_patches() {
	find "$1" -type f -name "*.patch" | sort
}

find_patch_dirs() {
	find_patches "$1" | xargs dirname | sort | uniq
}

count_lines() {
	echo "$1" | wc -l
}

encode_path() {
	STR="$1"

	if echo "$STR" | grep -q "__"; then
		echo "FIXME: Handle __ in path name"
		exit 1
	fi

	if echo "$STR" | grep -q "#"; then
		echo "FIXME: Handle # in path name"
		exit 1
	fi

	STR=$(echo "$STR" | sed 's/_/__/g')
	STR=$(echo "$STR" | tr '/' '_')

	echo "$STR"
}

decode_path() {
	STR="$1"

	STR=$(echo "$STR" | sed 's/__/#/g')
	STR=$(echo "$STR" | tr '_' '/')
	STR=$(echo "$STR" | tr '#' '_')

	echo "$STR"
}

extract_refs() {
	URL="$1"

	git ls-remote --refs "$URL" | cut -f2 | sed -e 's#^refs/heads/##'
}

match_refs() {
	REFS="$1"
	PROJECT="$2"
	BASE_REF="$3"

	echo "$REFS" | grep -E "^$PROJECT/$BASE_REF"
}

extract_path() {
	REF="$1"
	PROJECT="$2"
	BASE_REF="$3"

	echo "$REF" | sed -e "s#^$PROJECT/$BASE_REF/##"
}

add_remote() {
	REMOTE_NAME="$1"
	REMOTE_URL="$2"

	EXISTING_REMOTE_URL=$(git config "remote.$REMOTE_NAME.url")
	if [[ -n "$EXISTING_REMOTE_URL" ]]; then
		if [[ "$EXISTING_REMOTE_URL" != "$REMOTE_URL" ]]; then
			echo "Remote $REMOTE_NAME exists but URL is different"
			exit 1
		fi
	else
		git remote add "$REMOTE_NAME" "$REMOTE_URL"
	fi
}

fetch_remote_ref() {
	REMOTE_NAME="$1"
	BRANCH="$2"

	git fetch -q "$REMOTE_NAME" "$BRANCH" > /dev/null 2>&1

	return $?
}

push_head_to_branch() {
	REMOTE_NAME="$1"
	BRANCH_NAME="$2"
	FORCE="$3"

	if [[ -n "$FORCE" ]]; then
		FORCE_ARG="--force"
	fi

	git push $FORCE_ARG "$REMOTE_NAME" HEAD:"$BRANCH_NAME"
}

reset_to_fetched_ref() {
	git reset -q --hard FETCH_HEAD
}

init_repo() {
	REPO_PATH="$1"

	if [[ ! -d "$REPO_PATH" ]]; then
		mkdir -p "$REPO_PATH"
		git init -q -b master "$REPO_PATH"
	fi
}

count_patches() {
	RANGE="$1"

	git rev-list --count "$RANGE"
}

format_patches_to_output() {
	RANGE="$1"
	ROOT="$2"
	OUTPUT_PATH="$3"

	if [[ -n "$ROOT" ]]; then
		ROOT_ARG="--root"
	fi

	git format-patch --output-directory "$OUTPUT_PATH" $ROOT_ARG "$RANGE"
}

get_root_commit() {
	git rev-list --max-parents=0 HEAD
}

apply_patch() {
	PATCH="$1"

	git am --empty=keep "$PATCH"
}

compose_branch_name() {
	PROJECT="$1"
	TAG_NAME="$2"
	REPO_PATH_REL="$3"

	ENCODED_PATH=$(encode_path "$REPO_PATH_REL")
	echo "$PROJECT/$TAG_NAME/$ENCODED_PATH"
}

find_monorepo_paths() {
	BASE_PATH="$1"
	REMOTE_NAME="$2"
	PROJECT="$3"
	TAG_NAME="$4"

	REPO_REFS=$(extract_refs "$REMOTE_NAME")
	MATCHING_BRANCHES=$(match_refs "$REPO_REFS" "$PROJECT" "$TAG_NAME")

	while read -r BRANCH
	do
		ENCODED_PATH=$(extract_path "$BRANCH" "$PROJECT" "$TAG_NAME")
		REPO_PATH_REL=$(decode_path "$ENCODED_PATH")
		echo "$BASE_PATH/$REPO_PATH_REL"
	done <<< "$MATCHING_BRANCHES"
}
