#!/usr/bin/env python3
"""Verify that the cold slot pool is mapped by a writable+executable PT_LOAD."""

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


def find_symbol(readelf: str, elf: str, symbol: str) -> tuple[int, int]:
    out = run_tool([readelf, "-sW", elf])
    for line in out.splitlines():
        if not line.rstrip().endswith(symbol):
            continue
        match = re.match(
            r"\s*\d+:\s+([0-9a-fA-F]+)\s+(\d+)\s+\S+\s+\S+\s+\S+\s+(\S+)\s+(\S+)",
            line,
        )
        if match and match.group(4) == symbol:
            return int(match.group(1), 16), int(match.group(2))
    raise VerifyError(f"symbol {symbol} not found in {elf}")


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
        segments.append({
            "vaddr": int(match.group(2), 16),
            "memsz": int(match.group(5), 16),
            "flags": match.group(6).strip(),
        })
    return segments


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--elf", required=True)
    parser.add_argument("--symbol", default="cold_code_slot_pool")
    args = parser.parse_args(argv)

    try:
        address, size = find_symbol(args.readelf, args.elf, args.symbol)
        if size == 0:
            raise VerifyError(f"symbol {args.symbol} has size 0")

        segments = load_segments(args.readelf, args.elf)
        containing = [
            seg for seg in segments
            if seg["vaddr"] <= address and address + size <= seg["vaddr"] + seg["memsz"]
        ]
        if not containing:
            raise VerifyError(
                f"{args.symbol} [0x{address:x}, 0x{address + size:x}) is not covered "
                "by any PT_LOAD segment"
            )
        if len(containing) > 1:
            raise VerifyError(f"{args.symbol} is covered by multiple PT_LOAD segments")

        segment = containing[0]
        if "W" not in segment["flags"] or "E" not in segment["flags"]:
            raise VerifyError(
                f"{args.symbol} is in a PT_LOAD with flags '{segment['flags']}'; "
                "the slot pool must be writable and executable"
            )

        for other in segments:
            if other is segment:
                continue
            if address < other["vaddr"] + other["memsz"] and other["vaddr"] < address + size:
                raise VerifyError(
                    f"{args.symbol} overlaps another PT_LOAD at 0x{other['vaddr']:x}"
                )
    except VerifyError as exc:
        print(f"verify_cold_ram: error: {exc}", file=sys.stderr)
        return 1

    print(f"cold_code_slot_pool: 0x{address:x} size 0x{size:x} "
          f"in PT_LOAD flags {segment['flags']}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
