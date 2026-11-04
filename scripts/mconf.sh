#!/usr/bin/env bash

set -e

if command -v menuconfig &> /dev/null && command -v genconfig &> /dev/null; then
	# Prefer the python version
	menuconfig Kconfig
	genconfig --header-path ./include/generated/autoconf.h
else
	# Clone the out of tree linux tool
	if [[ ! -d tools/menuconfig ]]; then
		git clone https://github.com/anatol/menuconfig.git tools/menuconfig --depth=1
	fi

	ninja -C ./tools/menuconfig
	./tools/menuconfig/mconf Kconfig

	# Only care about the autoconf.h header
	rm -f ./include/generated/rustc_cfg
	rm -rf ./include/config
fi
