#!/usr/bin/env bash

set -e

cd "$(dirname "$0")/.."

if ! command -v bear > /dev/null 2>&1; then
	echo "bear not found in PATH" >&2
	exit 1
fi

if [[ ! -f .config ]]; then
	echo "Please configure the kernel before running this script" >&2
	exit 1
fi

VERSION_LINE=$(bear --version)
VERSION=${VERSION_LINE##* }
MAJOR=${VERSION%%.*}
 
if [[ -z "$MAJOR" ]]; then
	echo "Could not determine bear version" >&2
	exit 1
fi

make clean

if [[ $MAJOR -ge 3 ]]; then
	bear -- make "$@"
elif [[ $MAJOR -eq 2 ]]; then
	bear make "$@"
else
	echo "bear $VERSION is too old, need 2.x or newer" >&2
	exit 1
fi
