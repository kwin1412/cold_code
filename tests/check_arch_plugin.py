#!/usr/bin/env python3
"""Check that the x86-64 backend metadata matches the public header."""

from __future__ import annotations

import argparse
import re
import struct
import sys


def c_define(text: str, name: str) -> int | None:
    match = re.search(rf"#define\s+{name}\s+(\d+)u?", text)
    return int(match.group(1)) if match else None


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool-dir", required=True)
    parser.add_argument("--include-dir", required=True)
    args = parser.parse_args(argv)

    sys.path.insert(0, args.tool_dir)
    from arch import get_backend

    info = get_backend("x86_64").info
    failures: list[str] = []

    version_text = open(f"{args.include_dir}/cold_code/version.h", encoding="utf-8").read()
    match = re.search(r"COLD_CODE_ARCH_X86_64\s*=\s*(\d+)", version_text)
    if not match or int(match.group(1)) != info.arch_id:
        failures.append("arch id mismatch between backend and version.h")

    generator_text = open(f"{args.tool_dir}/gen_cold_code.py", encoding="utf-8").read()
    format_match = re.search(r'HEADER_FORMAT\s*=\s*"([^"]+)"', generator_text)
    header_size = struct.calcsize(format_match.group(1)) if format_match else None
    generator_values = {
        "COLD_CODE_IMAGE_VERSION": int(
            re.search(r"^IMAGE_VERSION\s*=\s*(\d+)", generator_text, re.M).group(1)
        ),
        "COLD_CODE_ABI_VERSION": int(
            re.search(r"^ABI_VERSION\s*=\s*(\d+)", generator_text, re.M).group(1)
        ),
        "COLD_CODE_IMAGE_HEADER_SIZE": header_size,
        "COLD_CODE_MAX_POOL_SLOTS": int(
            re.search(r"^MAX_POOL_SLOTS\s*=\s*(\d+)", generator_text, re.M).group(1)
        ),
    }
    for name, expected in generator_values.items():
        actual = c_define(version_text, name)
        if actual != expected:
            failures.append(f"{name}: version.h value {actual} != generator {expected}")

    if not info.supports_slow:
        failures.append("x86-64 backend must support slow stubs")

    if failures:
        for failure in failures:
            print(f"arch_plugin: {failure}", file=sys.stderr)
        return 1
    print("arch plugin ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
