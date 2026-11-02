#!/usr/bin/env python3
"""Verify that cold_code markers are mapped by writable+executable PT_LOADs."""

from __future__ import annotations

import argparse
import re
import subprocess
import sys


class VerifyError(Exception):
    pass


def run_tool(cmd: list[str]) -> str:
    try:
        return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    except FileNotFoundError as exc:
        raise VerifyError(f"tool not found: {cmd[0]}") from exc
    except subprocess.CalledProcessError as exc:
        detail = (exc.stderr or exc.stdout or "").strip()
        raise VerifyError(f"command failed: {' '.join(cmd)}\n{detail}") from exc


def find_symbols(readelf: str, elf: str, exact: str | None, prefix: str | None) -> list[tuple[str, int, int]]:
    out = run_tool([readelf, "-sW", elf])
    found = []
    for line in out.splitlines():
        match = re.match(
            r"\s*\d+:\s+([0-9a-fA-F]+)\s+(\d+)\s+.*?\s(\S+)\s*$",
            line,
        )
        if not match:
            continue
        name = match.group(3)
        if (exact is not None and name == exact) or (prefix is not None and name.startswith(prefix)):
            found.append((name, int(match.group(1), 16), int(match.group(2))))
    return found


def load_segments(readelf: str, elf: str) -> list[dict]:
    out = run_tool([readelf, "-lW", elf])
    segments = []
    for line in out.splitlines():
        match = re.match(
            r"\s*LOAD\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+"
            r"(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+([RWE ]+?)\s+(0x[0-9a-fA-F]+)",
            line,
        )
        if not match:
            continue
        segments.append(
            {
                "vaddr": int(match.group(2), 16),
                "memsz": int(match.group(5), 16),
                "flags": match.group(6).strip(),
            }
        )
    return segments


def verify_symbol(name: str, address: int, size: int, segments: list[dict]) -> None:
    if size == 0:
        raise VerifyError(f"symbol {name} has size 0")
    containing = [
        seg
        for seg in segments
        if seg["vaddr"] <= address and address + size <= seg["vaddr"] + seg["memsz"]
    ]
    if not containing:
        raise VerifyError(
            f"{name} [0x{address:x}, 0x{address + size:x}) is not covered by any PT_LOAD"
        )
    if len(containing) > 1:
        raise VerifyError(f"{name} is covered by multiple PT_LOAD segments")
    segment = containing[0]
    if "W" not in segment["flags"] or "E" not in segment["flags"]:
        raise VerifyError(
            f"{name} is in a PT_LOAD with flags '{segment['flags']}'; "
            "the cold-code pool must be writable and executable"
        )
    print(f"{name}: 0x{address:x} size 0x{size:x} in PT_LOAD flags {segment['flags']}")


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--elf", required=True)
    parser.add_argument("--symbol", required=True)
    parser.add_argument("--prefix")
    args = parser.parse_args(argv)

    try:
        segments = load_segments(args.readelf, args.elf)
        exact = find_symbols(args.readelf, args.elf, args.symbol, None)
        if not exact:
            raise VerifyError(f"symbol {args.symbol} not found in {args.elf}")
        for name, address, size in exact:
            verify_symbol(name, address, size, segments)

        if args.prefix:
            matches = find_symbols(args.readelf, args.elf, None, args.prefix)
            if not matches:
                print(
                    f"verify_rwx: warning: no storage marker with prefix {args.prefix}; "
                    "storage placement was not verified",
                    file=sys.stderr,
                )
            for name, address, size in matches:
                verify_symbol(name, address, size, segments)
    except VerifyError as exc:
        print(f"verify_rwx: error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
