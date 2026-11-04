#!/usr/bin/env bash

set -e

source $(dirname "$0")/ver.sh

C_COMPILER="$1"
VERSION_STR=$("$C_COMPILER" -dumpversion 2>/dev/null)
if [[ -z "$VERSION_STR" ]]; then
	>&2 echo "Cannot get compiler version"
	exit 1
fi

check_version "$C_COMPILER" "$VERSION_STR" "$2" "$3"
