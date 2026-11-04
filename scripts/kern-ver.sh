#!/usr/bin/env bash

set -e

OUTPUT_HEADER="$1"
KERNEL_MAJOR="$2"
KERNEL_MINOR="$3"

if [[ -z "$OUTPUT_HEADER" || -z "$KERNEL_MAJOR" || -z "$KERNEL_MINOR" ]]; then
	echo "Usage: <output_header> <kernel_major> <kernel_minor>"
	exit 1
fi

# Only write the file if something changed, otherwise the entire kernel will be rebuilt every time this script is used
HEADER="$(printf "#pragma once\n\n#define CRATER_MAJOR %d\n#define CRATER_MINOR %d" "$KERNEL_MAJOR" "$KERNEL_MINOR")"
if [[ ! -f "$OUTPUT_HEADER" || "$HEADER" != "$(cat "$OUTPUT_HEADER")" ]]; then
	printf "%s\n" "$HEADER" > "$OUTPUT_HEADER"
fi
