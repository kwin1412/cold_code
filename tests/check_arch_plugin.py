#!/usr/bin/env python3
"""Check that the x86-64 backend metadata matches the public/arch headers."""

from __future__ import annotations

import argparse
import re
import sys


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool-dir", required=True)
    parser.add_argument("--include-dir", required=True)
    parser.add_argument("--arch-include-dir", required=True)
    args = parser.parse_args(argv)

    sys.path.insert(0, args.tool_dir)
    from arch import get_backend

    info = get_backend("x86_64").info
    failures: list[str] = []

    version_text = open(f"{args.include_dir}/cold_code/version.h", encoding="utf-8").read()
    match = re.search(r"COLD_CODE_ARCH_X86_64\s*=\s*(\d+)", version_text)
    if not match or int(match.group(1)) != info.arch_id:
        failures.append("arch id mismatch between backend and version.h")

    abi_text = open(f"{args.include_dir}/cold_code/abi/v1.h", encoding="utf-8").read()
    match = re.search(r"COLD_CODE_MANAGER_STATE_READY\s+(0x[0-9a-fA-F]+|\d+)", abi_text)
    if not match or int(match.group(1), 0) != 1:
        failures.append("COLD_CODE_MANAGER_STATE_READY is not the expected ready bit")

    arch_header = open(
        f"{args.arch_include_dir}/cold_code/abi/v1/x86_64.h", encoding="utf-8"
    ).read()
    defines = {
        name: int(value, 0)
        for name, value in re.findall(
            r"#define\s+(COLD_CODE_FAST_[A-Z0-9_]+)\s+(\d+)", arch_header
        )
    }
    expected = {
        "COLD_CODE_FAST_ABI_VERSION": info.fast_abi_version,
        "COLD_CODE_FAST_STATE_SIZE": 56,
        "COLD_CODE_FAST_OFF_IMAGE": 0,
        "COLD_CODE_FAST_OFF_SLOT_MAP": 8,
        "COLD_CODE_FAST_OFF_WAY_USED": 16,
        "COLD_CODE_FAST_OFF_POOL": 24,
        "COLD_CODE_FAST_OFF_POOL_SLOTS": 32,
        "COLD_CODE_FAST_OFF_STATE": 36,
        "COLD_CODE_FAST_OFF_TICK": 40,
        "COLD_CODE_FAST_OFF_HITS": 48,
    }
    for name, value in expected.items():
        if defines.get(name) != value:
            failures.append(f"{name}: backend/header value {defines.get(name)} != {value}")

    if info.fast_accessor != "cold_code_context_current_fast":
        failures.append("unexpected fast accessor name")

    if failures:
        for failure in failures:
            print(f"arch_plugin: {failure}", file=sys.stderr)
        return 1
    print("arch plugin ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
