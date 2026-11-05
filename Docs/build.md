Build
=====
Crater is built with Make and configured with Kconfig. The build produces one executable `crater`, which is meant to be loaded by the [Limine](https://github.com/limine-bootloader/limine) bootloader (or any bootloader that supports the [limine protocol](https://github.com/Limine-Bootloader/limine-protocol/blob/trunk/PROTOCOL.md)). x86_64 is the only supported architecture.

## Quick Start

```sh
git clone https://github.com/primeseven1/crater.git --recurse-submodules
cd crater
make menuconfig
make
```

If you cloned without `--recurse-submodules`, run `git submodule update --init --recursive` before building.

## Required Tools

| Tool | Needed for | Minimum version
| --- | --- | ---
| `make`, `bash`, `find`, `git` | Build system and helper scripts | -
| `sparse` | Static checking | -
| `x86_64-elf-gcc` | C compiler | 12.2
| `x86_64-elf-ld` | Linker | 2.39
| `clang`, `ld.lld` | LLVM toolchain (CONFIG_LLVM=y) | 14.0
| `kconfiglib` (Python), or `ninja` | `make menuconfig` | -

## Submodules

| Path | Project | Needed for
| --- | --- | ---
| `drivers/acpi/uacpi` | [uACPI](https://github.com/uACPI/uACPI) | ACPI support, required
| `drivers/liminefb/flanterm` | [Flanterm](https://github.com/Mintsuki/Flanterm.git) | Framebuffer terminal support

Clone with `--recurse-submodules`, or initialize them afterwards:

```sh
git submodule update --init --recursive
```

Use the pinned commits, or the build might break.

## Configuration

Run:
```sh
make menuconfig
```

This will run `scripts/mconf.sh`, which picks a Kconfig frontend:

1. If both `menuconfig` and `genconfig` are in `PATH` (from the `kconfiglib` package), it uses them.
2. Otherwise it clones https://github.com/anatol/menuconfig into `tools/menuconfig`, builds with `ninja`, and runs it.

Two files are produced:
- `.config`: The Makefile reads it, and uses the selected options as make variables
- `include/generated/autoconf.h`: The selected options as C macros, it is force included into each file

## Building
```sh
make # Same as "make all"
```
## Running
This repository only builds the kernel binary. It does not create a bootable disk image or start an emulator.

There is a repository [Crater Userspace](https://github.com/primeseven1/crater-userspace) that creates a bootable ISO.

## Troubleshooting

| Symptom | Cause and fix
| --- | ---
| `-elf-gcc: command not found`, or `autoconf.h` not found | No .config yet. Run make menuconfig
