#!/usr/bin/env python3
"""Reject the same global cold function name in two images of one target."""

from __future__ import annotations

import argparse
import sys


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--symbols", nargs="+", required=True)
    args = parser.parse_args(argv)

    owners: dict[str, str] = {}
    image_ids: dict[int, str] = {}
    for path in args.symbols:
        try:
            with open(path, encoding="utf-8") as handle:
                lines = [line.strip() for line in handle if line.strip()]
        except OSError as exc:
            print(f"check_duplicate_symbols: error: {exc}", file=sys.stderr)
            return 1

        for line in lines:
            if line.startswith("#"):
                parts = line.split()
                if len(parts) == 5 and parts[1] == "image" and parts[3] == "id":
                    try:
                        image_id = int(parts[4], 16)
                    except ValueError:
                        print(f"check_duplicate_symbols: error: bad image id line in {path}: {line}",
                              file=sys.stderr)
                        return 1
                    previous = image_ids.get(image_id)
                    if previous is not None and previous != path:
                        print(
                            f"check_duplicate_symbols: error: image id 0x{image_id:08x} is "
                            f"used by two images of the same target ({previous} and {path})",
                            file=sys.stderr,
                        )
                        return 1
                    image_ids[image_id] = path
                continue

            previous = owners.get(line)
            if previous is not None and previous != path:
                print(
                    f"check_duplicate_symbols: error: cold function '{line}' is defined "
                    f"in two images of the same target ({previous} and {path})",
                    file=sys.stderr,
                )
                return 1
            owners[line] = path
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
