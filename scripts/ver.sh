check_version() {
	local TOOL="$1"
	local VERSION_STR="$2"
	local MIN_MAJOR="$3"
	local MIN_MINOR="$4"

	local MAJOR=$(echo "$VERSION_STR" | cut -d. -f1)
	local MINOR=$(echo "$VERSION_STR" | cut -d. -f2)
	if [[ -z "$MAJOR" ]]; then
		>&2 echo "$TOOL major version is empty"
		return 1
	fi
	if [[ -z "$MINOR" ]]; then
		>&2 echo "$TOOL minor version is empty"
		return 1
	fi

	if [[ "$MAJOR" -gt "$MIN_MAJOR" ]] || ([[ "$MAJOR" -eq "$MIN_MAJOR" ]] && [[ "$MINOR" -ge "$MIN_MINOR" ]]); then
		return 0
	fi

	{
		echo "$TOOL version is too old"
		echo "Current version: $MAJOR.$MINOR"
		echo "Required version: $MIN_MAJOR.$MIN_MINOR"
	} >&2

	return 1
}
