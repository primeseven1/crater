#!/usr/bin/env python

import sys
import os
import subprocess
import shlex
import json
import argparse

def filter_args(args: list[str], src: str, directory: str, verbose: bool) -> list[str]:
    # note: skip_with_value skips ANY argument that starts with the character, this does not need a fix right now since there are no conflicting flags like that being ignored
    skip_with_value: tuple[str, ...] = ("-o", "-MF", "-MT", "-MQ")
    skip: set[str] = {"-c", "-MMD", "-MD", "-MP", "-MM", "-M"}
    src_real: str = os.path.realpath(src)

    out: list[str] = []
    it = iter(args)
    for a in it:
        matched: str | None = next((f for f in skip_with_value if a.startswith(f)), None)
        if matched is not None:
            if a == matched:
                # argument is separated (eg. -o ./a.out)
                next(it, None)
            # argument is together (eg. -oa.out/-DSOMETHING=1)
            continue
        if a in skip:
            continue
        # drop the source file
        if not a.startswith("-") and os.path.realpath(os.path.join(directory, a)) == src_real:
            continue
        out.append(a)

    if verbose and "-fverbose-asm" not in out:
        out.append("-fverbose-asm")
    return out

def load_flags(abspd: str, file: str, verbose: bool) -> list[str]:
    cc_path: str = os.path.join(abspd, "..", "compile_commands.json")
    if not os.path.isfile(cc_path):
        return []
    with open(cc_path, "r") as f:
        entries: list[dict] = json.load(f)

    target: str = os.path.realpath(file)
    for entry in entries:
        entry_file: str = os.path.join(entry["directory"], entry["file"])
        if os.path.realpath(entry_file) != target:
            continue

        if "arguments" in entry:
            args: list[str] = entry["arguments"]
        else:
            args = shlex.split(entry["command"])

        # drop the compiler executable argument with 1:
        return filter_args(args[1:], entry_file, entry["directory"], verbose)
    return []

def parse_args() -> argparse.Namespace:
    ret: argparse.ArgumentParser = argparse.ArgumentParser(prog="asm", description="Check assembly output for the kernel C source files",
                                                           epilog="Use scripts/bear.sh script to generate a compile_commands.json (required)")
    ret.add_argument("filename", help="Input file", type=str)
    ret.add_argument("-v", "--verbose", action="store_true", help="Apply the -fverbose-asm option")
    ret.add_argument("-O", "--opt", type=int, help="Optimization level", choices=[0, 1, 2, 3], default=0)
    ret.add_argument("-c", "--compiler", type=str, help="Compiler name", default="x86_64-elf-gcc")
    ret.add_argument("-p", "--pager", type=str, help="Pager name", default="less")

    return ret.parse_args()

def main() -> int:
    args: argparse.Namespace = parse_args()

    abspd: str = os.path.dirname(os.path.abspath(__file__))
    file: str = os.path.abspath(args.filename)
    flags: list[str] = load_flags(abspd, file, args.verbose)
    cmd: list[str] = [args.compiler, "-S", file, *flags, f"-O{args.opt}", "-o", "-"]
    cwd: str = os.path.abspath(os.path.join(abspd, ".."))
    try:
        gcc_sp = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd, check=True)
    except subprocess.CalledProcessError as e:
        print(e.stderr.decode(errors="replace"), file=sys.stderr, end="")
        return 1

    subprocess.run([args.pager], input=gcc_sp.stdout)
    return 0

if __name__ == "__main__":
    sys.exit(main())
