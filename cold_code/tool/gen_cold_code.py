#!/usr/bin/env python3
"""Generate a v3 cold image, hot stubs, descriptor and opaque storage type.

The cold image is an ELF linked at VMA 0 with only .cold_text kept.  The
arch backend owns relocation/call validation and stub emission; this script
owns the arch-neutral layout, image v3 wire header, descriptor/storage
generation and diagnostics.
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import subprocess
import sys
import zlib

from arch import ArchError, get_backend

IMAGE_NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
HEADER_FORMAT = "<IHH14I"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
IMAGE_MAGIC = 0x444C4F43  # bytes "COLD" little-endian
IMAGE_VERSION = 3
ABI_VERSION = 1
IMAGE_FLAG_PAYLOAD_CRC32 = 1
MAX_POOL_SLOTS = 16


class GenError(Exception):
    pass


def run_tool(cmd: list[str]) -> str:
    try:
        return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    except FileNotFoundError as exc:
        raise GenError(f"tool not found: {cmd[0]}") from exc
    except subprocess.CalledProcessError as exc:
        detail = (exc.stderr or exc.stdout or "").strip()
        raise GenError(f"command failed: {' '.join(cmd)}\n{detail}") from exc


def parse_section(readelf: str, elf: str, section: str) -> tuple[int, int]:
    out = run_tool([readelf, "-SW", elf])
    for line in out.splitlines():
        match = re.match(
            r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-fA-F]+)\s+"
            r"[0-9a-fA-F]+\s+([0-9a-fA-F]+)",
            line,
        )
        if match and match.group(1) == section:
            return int(match.group(3), 16), int(match.group(4), 16)
    raise GenError(f"section {section} not found in {elf}")


def parse_symbols(nm: str, elf: str) -> list[dict]:
    out = run_tool([nm, "-S", "--defined-only", "--format=posix", elf])
    symbols = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) != 4:
            continue
        name, kind, value_s, size_s = parts
        if kind not in "TtWw":
            continue
        try:
            value = int(value_s, 16)
            size = int(size_s, 16)
        except ValueError:
            continue
        symbols.append({"name": name, "type": kind, "value": value, "size": size})
    return symbols


def check_undefined(nm: str, elf: str) -> None:
    out = run_tool([nm, "-u", elf]).strip()
    if out:
        raise GenError(f"cold image has undefined symbols:\n{out}")


def check_no_straddle(functions: list[dict], slot_size: int) -> None:
    for fn in functions:
        first = fn["offset"] // slot_size
        last = (fn["offset"] + fn["size"] - 1) // slot_size
        if first != last:
            raise GenError(
                f"{fn['name']} (offset 0x{fn['offset']:x}, size {fn['size']}) spans "
                f"image slots {first}..{last}; every cold function must fit inside one "
                "slot. Increase SLOT_SIZE or restructure the cold functions."
            )


def write_text(path: str, text: str) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


def require_args(args: argparse.Namespace) -> None:
    required = [
        "nm",
        "readelf",
        "objdump",
        "elf",
        "raw_bin",
        "final_bin",
        "image_name",
        "slot_size",
        "pool_slots",
        "image_header",
        "image_source",
        "stubs",
        "layout_map",
        "symbols",
    ]
    missing = [name for name in required if getattr(args, name) is None]
    if missing:
        raise GenError("missing required arguments: " + ", ".join("--" + n.replace("_", "-")
                                                                  for n in missing))


def generate(args: argparse.Namespace) -> None:
    require_args(args)
    if not IMAGE_NAME_RE.match(args.image_name):
        raise GenError(f"invalid image name '{args.image_name}'; use a C identifier")
    if args.slot_size <= 0:
        raise GenError("slot size must be positive")
    if args.pool_slots <= 0 or args.pool_slots > MAX_POOL_SLOTS:
        raise GenError(f"pool slots must be between 1 and {MAX_POOL_SLOTS}")

    backend = get_backend(args.arch)
    info = backend.info
    if args.slot_size % info.pool_alignment != 0:
        raise GenError(
            f"slot size {args.slot_size} must be a multiple of the {info.name} pool "
            f"alignment {info.pool_alignment}"
        )

    mode = args.stubs_mode
    if mode == "fast" and not info.supports_fast:
        raise GenError(f"arch backend '{info.name}' does not support fast stubs")
    if mode == "slow" and not info.supports_slow:
        raise GenError(f"arch backend '{info.name}' does not support slow stubs")

    base, section_size = parse_section(args.readelf, args.elf, ".cold_text")
    with open(args.raw_bin, "rb") as handle:
        raw = handle.read()
    if not raw:
        raise GenError("the cold image is empty")
    if len(raw) < section_size:
        raise GenError(
            f"cold binary is {len(raw)} bytes, smaller than .cold_text "
            f"({section_size} bytes)"
        )

    symbols = parse_symbols(args.nm, args.elf)
    functions = []
    for sym in symbols:
        offset = sym["value"] - base
        if offset < 0 or offset + sym["size"] > len(raw):
            if sym["type"] in "TW":
                raise GenError(f"global function {sym['name']} lies outside the cold image")
            continue
        if sym["size"] == 0:
            if sym["type"] in "TW":
                raise GenError(
                    f"global function {sym['name']} has size 0; IPA-ICF may have merged "
                    "it. Build the cold image with -O1 (ICF disabled) or -fno-ipa-icf."
                )
            continue
        sym["offset"] = offset
        functions.append(sym)

    globals_ = sorted((fn for fn in functions if fn["type"] in "TW"),
                      key=lambda fn: fn["offset"])
    if not globals_:
        raise GenError("the cold image contains no global COLD_CODE functions")

    ordered = sorted(functions, key=lambda fn: (fn["offset"], -fn["size"]))
    for prev, cur in zip(ordered, ordered[1:]):
        if cur["offset"] != prev["offset"] and cur["offset"] < prev["offset"] + prev["size"]:
            raise GenError(f"cold functions {prev['name']} and {cur['name']} overlap")

    image_slot_count = (len(raw) + args.slot_size - 1) // args.slot_size
    if image_slot_count == 0 or image_slot_count > 0xFFFFFFFF:
        raise GenError("invalid image slot count")
    max_pool_slots = min(args.pool_slots, image_slot_count)

    check_undefined(args.nm, args.elf)
    backend.check_relocations(run_tool([args.readelf, "-rW", args.elf]))
    check_no_straddle(functions, args.slot_size)
    backend.check_cross_slot_calls(
        run_tool([args.objdump, "-d", "--no-show-raw-insn", args.elf]),
        base,
        functions,
        args.slot_size,
    )

    padded = raw + b"\0" * (image_slot_count * args.slot_size - len(raw))
    payload_crc = zlib.crc32(padded) & 0xFFFFFFFF
    image_id = zlib.crc32(args.image_name.encode("utf-8")) & 0xFFFFFFFF

    header = bytearray(
        struct.pack(
            HEADER_FORMAT,
            IMAGE_MAGIC,
            IMAGE_VERSION,
            HEADER_SIZE,
            image_id,
            info.arch_id,
            ABI_VERSION,
            args.slot_size,
            image_slot_count,
            max_pool_slots,
            HEADER_SIZE,
            len(padded),
            payload_crc,
            IMAGE_FLAG_PAYLOAD_CRC32,
            0,  # header CRC, patched below
            0,
            0,
            0,
        )
    )
    assert len(header) == HEADER_SIZE
    struct.pack_into("<I", header, 48, zlib.crc32(header) & 0xFFFFFFFF)

    os.makedirs(os.path.dirname(args.final_bin) or ".", exist_ok=True)
    with open(args.final_bin, "wb") as handle:
        handle.write(header)
        handle.write(padded)

    upper = args.image_name.upper()
    layout_lines = [
        "# Cold-code image layout (generated by cold_code/tool/gen_cold_code.py)",
        f"image_name      {args.image_name}",
        f"image_id        0x{image_id:08x}",
        f"arch            {info.name} ({info.arch_id})",
        f"abi_version     {ABI_VERSION}",
        f"header_bytes    {HEADER_SIZE}",
        f"payload_offset  0x{HEADER_SIZE:x}",
        f"payload_crc32   0x{payload_crc:08x}",
        f"slot_size       {args.slot_size}",
        f"image_slots     {image_slot_count}",
        f"pool_slots      {max_pool_slots}",
        f"storage_align   {info.pool_alignment}",
        f"raw_bytes       0x{len(raw):x}",
        f"padded_bytes    0x{len(padded):x}",
        "",
        "# kind    function                          offset    size      slot  intra",
    ]
    for fn in sorted(functions, key=lambda item: item["offset"]):
        kind = "global" if fn["type"] in "TW" else "local"
        slot = fn["offset"] // args.slot_size
        intra = fn["offset"] % args.slot_size
        layout_lines.append(
            f"{kind:<7} {fn['name']:<33} 0x{fn['offset']:08x} "
            f"0x{fn['size']:08x}  {slot:>4}  0x{intra:04x}"
        )
    layout_lines.append("")
    write_text(args.layout_map, "\n".join(layout_lines))

    symbol_lines = [
        f"# image {args.image_name} id 0x{image_id:08x}\n",
        *(f"{fn['name']}\n" for fn in globals_),
    ]
    write_text(args.symbols, "".join(symbol_lines))

    guard = f"COLD_CODE_IMAGE_{upper}_H"
    header_lines = [
        "/* Auto-generated by cold_code/tool/gen_cold_code.py. Do not edit. */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        '#include "cold_code.h"',
        "",
        f'#define COLD_CODE_IMAGE_{upper}_NAME "{args.image_name}"',
        f"#define COLD_CODE_IMAGE_{upper}_ID 0x{image_id:08x}u",
        f"#define COLD_CODE_IMAGE_{upper}_SLOT_SIZE {args.slot_size}u",
        f"#define COLD_CODE_IMAGE_{upper}_SLOT_COUNT {image_slot_count}u",
        f"#define COLD_CODE_IMAGE_{upper}_MAX_POOL_SLOTS {max_pool_slots}u",
        f"#define COLD_CODE_IMAGE_{upper}_STORAGE_ALIGN {info.pool_alignment}u",
        f"#define COLD_CODE_IMAGE_{upper}_STORAGE_BYTES \\",
        f"    COLD_CODE_STORAGE_SIZE({image_slot_count}u, {max_pool_slots}u, "
        f"{args.slot_size}u, {info.pool_alignment}u)",
        f'#define COLD_CODE_IMAGE_{upper}_FILE "cold_code_{args.image_name}.bin"',
        "",
        f"extern const cold_code_image_t cold_code_image_{args.image_name}",
        '    __attribute__((visibility("hidden")));',
        "",
        "typedef struct {",
        f"    unsigned char _bytes[COLD_CODE_IMAGE_{upper}_STORAGE_BYTES];",
        f"}} cold_code_storage_{args.image_name}_t",
        f"    __attribute__((aligned({info.pool_alignment})));",
        "",
        f"#endif /* {guard} */",
        "",
    ]
    write_text(args.image_header, "\n".join(header_lines))

    arch_enum = getattr(backend, "ARCH_ENUM", "COLD_CODE_ARCH_UNKNOWN")
    source_lines = [
        "/* Auto-generated by cold_code/tool/gen_cold_code.py. Do not edit. */",
        f'#include "cold_code_{args.image_name}_image.h"',
        "",
        f"const cold_code_image_t cold_code_image_{args.image_name}",
        '    __attribute__((visibility("hidden"))) = {',
        f'    .image_id = 0x{image_id:08x}u,',
        f"    .slot_size = {args.slot_size}u,",
        f"    .slot_count = {image_slot_count}u,",
        f"    .max_pool_slots = {max_pool_slots}u,",
        f"    .arch = {arch_enum},",
        "    .abi_version = COLD_CODE_ABI_VERSION,",
        "    .struct_size = sizeof(cold_code_image_t),",
        f"    .storage_align = {info.pool_alignment}u,",
        f'    .name = "{args.image_name}",',
        "};",
        "",
    ]
    write_text(args.image_source, "\n".join(source_lines))

    accessor = args.fast_accessor or info.fast_accessor
    write_text(
        args.stubs,
        backend.emit_stubs(
            args.image_name,
            globals_,
            args.slot_size,
            mode == "fast",
            accessor,
        ),
    )


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", required=True)
    parser.add_argument("--print-arch-info", action="store_true")
    parser.add_argument("--print-cflags", action="store_true")
    parser.add_argument("--stubs-mode", choices=("slow", "fast"), default="slow")
    parser.add_argument("--fast-accessor")
    parser.add_argument("--nm")
    parser.add_argument("--readelf")
    parser.add_argument("--objdump")
    parser.add_argument("--elf")
    parser.add_argument("--raw-bin")
    parser.add_argument("--final-bin")
    parser.add_argument("--image-name")
    parser.add_argument("--slot-size", type=int)
    parser.add_argument("--pool-slots", type=int)
    parser.add_argument("--image-header")
    parser.add_argument("--image-source")
    parser.add_argument("--stubs")
    parser.add_argument("--layout-map")
    parser.add_argument("--symbols")
    args = parser.parse_args(argv)

    try:
        backend = get_backend(args.arch)
        info = backend.info
        if args.print_arch_info:
            print(f"arch={info.name}")
            print(f"arch_id={info.arch_id}")
            print(f"pool_alignment={info.pool_alignment}")
            print(f"supports_slow={int(info.supports_slow)}")
            print(f"supports_fast={int(info.supports_fast)}")
            print(f"fast_accessor={info.fast_accessor}")
            print(f"fast_abi_version={info.fast_abi_version}")
            return 0
        if args.print_cflags:
            print(" ".join(info.cold_cflags))
            return 0
        generate(args)
    except (GenError, ArchError) as exc:
        print(f"gen_cold_code: error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
