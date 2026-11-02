#!/usr/bin/env python3
"""Generate the v2 cold image, hot stubs, descriptor and storage bindings.

The cold image is an ELF linked at VMA 0 with only .cold_text kept.  This
script:
  * parses nm/readelf/objdump output,
  * rejects absolute relocations / GOT references and unsafe cross-slot calls,
  * requires every cold function to fit inside one image slot,
  * pads the payload to a whole number of slots,
  * writes a versioned image header (magic/version/image_id/CRC) followed by
    the payload,
  * emits namespaced hot stubs, the image descriptor and the generated storage
    type used by consumers.

There are no function IDs: a stub carries its image ID, image slot number and
the offset inside that slot, and calls cold_code_acquire_slot() on every
invocation so the slot-level LRU sees the access.
"""

from __future__ import annotations

import argparse
import os
import re
import struct
import subprocess
import sys
import zlib

MAX_POOL_SLOTS = 16
IMAGE_MAGIC = 0x444C4F43  # bytes "COLD" little-endian
IMAGE_VERSION = 2
HEADER_FORMAT = "<IHHIIIIIII"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
IMAGE_NAME_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


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


def parse_section(readelf: str, elf: str, section: str) -> tuple[int, int]:
    out = run_tool([readelf, "-SW", elf])
    for line in out.splitlines():
        match = re.match(r"\s*\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-fA-F]+)\s+"
                         r"[0-9a-fA-F]+\s+([0-9a-fA-F]+)", line)
        if match and match.group(1) == section:
            return int(match.group(3), 16), int(match.group(4), 16)
    raise GenError(f"section {section} not found in {elf}")


def check_relocations(readelf: str, elf: str) -> None:
    allowed = {
        "R_X86_64_NONE",
        "R_X86_64_PC32",
        "R_X86_64_PLT32",
        "R_X86_64_PC64",
        "R_X86_64_PC16",
        "R_X86_64_PC8",
    }
    out = run_tool([readelf, "-rW", elf])
    current_section = None
    for line in out.splitlines():
        section_match = re.match(r"Relocation section '([^']+)'", line)
        if section_match:
            current_section = section_match.group(1)
            continue
        if current_section is None or not current_section.startswith(".rela.cold_text"):
            continue
        match = re.match(r"\s*[0-9a-fA-F]+\s+[0-9a-fA-F]+\s+(R_[A-Z0-9_]+)\s+", line)
        if not match:
            continue
        kind = match.group(1)
        if kind not in allowed:
            raise GenError(
                f"unsupported relocation {kind} in the cold image; cold code must be "
                "position independent and self-contained (no GOT/PLT/data references)"
            )


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


def check_cross_slot_calls(objdump: str, elf: str, base: int, functions: list[dict],
                           slot_size: int) -> None:
    out = run_tool([objdump, "-d", "--no-show-raw-insn", elf])
    call_re = re.compile(
        r"^\s*([0-9a-fA-F]+):\s+(?:callq?|jmpq?)\s+"
        r"(0x[0-9a-fA-F]+|[0-9a-fA-F]+)\s+<([^>]+)>"
    )

    for line in out.splitlines():
        match = call_re.match(line)
        if not match:
            continue
        insn_rel = int(match.group(1), 16) - base
        target_rel = int(match.group(2), 16) - base

        caller = None
        for fn in functions:
            if fn["offset"] <= insn_rel < fn["offset"] + fn["size"]:
                caller = fn
                break
        if caller is None:
            continue

        callee = None
        for fn in functions:
            if fn["offset"] <= target_rel < fn["offset"] + fn["size"]:
                callee = fn
                break
        if callee is None:
            raise GenError(
                f"{caller['name']}: direct call target 0x{target_rel:x} is not a "
                "cold function; cold code must be self-contained"
            )
        if callee is caller:
            continue

        caller_slot = caller["offset"] // slot_size
        callee_slot = callee["offset"] // slot_size
        if caller_slot != callee_slot:
            raise GenError(
                f"{caller['name']} (image slot {caller_slot}) calls "
                f"{callee['name']} (image slot {callee_slot}); cross-slot cold calls "
                "are not supported with slot-level LRU. Put them in the same slot."
            )


def write_text(path: str, text: str) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(text)


def generate(args: argparse.Namespace) -> None:
    if not IMAGE_NAME_RE.match(args.image_name):
        raise GenError(f"invalid image name '{args.image_name}'; use a C identifier")
    if args.slot_size <= 0:
        raise GenError("slot size must be positive")
    if args.pool_slots <= 0 or args.pool_slots > MAX_POOL_SLOTS:
        raise GenError(f"pool slots must be between 1 and {MAX_POOL_SLOTS}")

    base, section_size = parse_section(args.readelf, args.elf, ".cold_text")

    with open(args.raw_bin, "rb") as handle:
        raw = handle.read()
    if not raw:
        raise GenError("the cold image is empty")
    if len(raw) < section_size:
        raise GenError(f"cold binary is {len(raw)} bytes, smaller than .cold_text "
                       f"({section_size} bytes)")

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

    # Overlapping ranges would make call-site attribution ambiguous.  Sharing a
    # start address (alias) is harmless for our purposes.
    ordered = sorted(functions, key=lambda fn: (fn["offset"], -fn["size"]))
    for prev, cur in zip(ordered, ordered[1:]):
        if cur["offset"] != prev["offset"] and cur["offset"] < prev["offset"] + prev["size"]:
            raise GenError(f"cold functions {prev['name']} and {cur['name']} overlap")

    image_slot_count = (len(raw) + args.slot_size - 1) // args.slot_size
    if image_slot_count == 0 or image_slot_count > 0xFFFFFFFF:
        raise GenError("invalid image slot count")
    pool_slot_count = min(args.pool_slots, image_slot_count)

    check_undefined(args.nm, args.elf)
    check_relocations(args.readelf, args.elf)
    check_no_straddle(functions, args.slot_size)
    check_cross_slot_calls(args.objdump, args.elf, base, functions, args.slot_size)

    # Pad the payload so every slot read is a whole slot.
    padded = raw + b"\0" * (image_slot_count * args.slot_size - len(raw))
    payload_crc = zlib.crc32(padded) & 0xFFFFFFFF
    image_id = zlib.crc32(args.image_name.encode("utf-8")) & 0xFFFFFFFF

    header = struct.pack(
        HEADER_FORMAT,
        IMAGE_MAGIC,
        IMAGE_VERSION,
        HEADER_SIZE,
        image_id,
        args.slot_size,
        image_slot_count,
        HEADER_SIZE,
        payload_crc,
        0,
        0,
    )
    assert len(header) == HEADER_SIZE

    os.makedirs(os.path.dirname(args.final_bin) or ".", exist_ok=True)
    with open(args.final_bin, "wb") as handle:
        handle.write(header)
        handle.write(padded)

    # --- Cold layout map ---------------------------------------------------
    layout_lines = [
        "# Cold-code image layout (generated by cold_code/tool/gen_cold_code.py)",
        f"image_name      {args.image_name}",
        f"image_id        0x{image_id:08x}",
        f"header_bytes    {HEADER_SIZE}",
        f"payload_offset  0x{HEADER_SIZE:x}",
        f"payload_crc32   0x{payload_crc:08x}",
        f"slot_size       {args.slot_size}",
        f"image_slots     {image_slot_count}",
        f"pool_slots      {pool_slot_count}",
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

    # --- Symbol list for cross-image duplicate detection -------------------
    symbol_lines = [
        f"# image {args.image_name} id 0x{image_id:08x}\n",
        *(f"{fn['name']}\n" for fn in globals_),
    ]
    write_text(args.symbols, "".join(symbol_lines))

    # --- Generated header --------------------------------------------------
    name = args.image_name
    upper = name.upper()
    guard = f"COLD_CODE_IMAGE_{upper}_H"
    header_lines = [
        "/* Auto-generated by cold_code/tool/gen_cold_code.py. Do not edit. */",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        '#include "cold_code.h"',
        "",
        f'#define COLD_CODE_IMAGE_{upper}_NAME "{name}"',
        f"#define COLD_CODE_IMAGE_{upper}_ID 0x{image_id:08x}u",
        f"#define COLD_CODE_IMAGE_{upper}_SLOT_SIZE {args.slot_size}u",
        f"#define COLD_CODE_IMAGE_{upper}_SLOT_COUNT {image_slot_count}u",
        f"#define COLD_CODE_IMAGE_{upper}_POOL_SLOTS {pool_slot_count}u",
        "",
        f"extern const cold_code_image_t cold_code_image_{name};",
        "",
        "typedef struct {",
        "    cold_code_storage_header_t header;",
        f"    uint32_t slot_map[{image_slot_count}];",
        f"    uint32_t way_slot[{pool_slot_count}];",
        f"    uint64_t way_used[{pool_slot_count}];",
        f"    unsigned char pool[{pool_slot_count}][{args.slot_size}]",
        "        __attribute__((aligned(64)));",
        f"}} cold_code_storage_{name}_t;",
        "",
        f"static inline void cold_code_storage_{name}_bind(",
        f"    cold_code_manager_t *mgr, cold_code_storage_{name}_t *storage) {{",
        "    mgr->storage = &storage->header;",
        "    mgr->slot_map = storage->slot_map;",
        "    mgr->way_slot = storage->way_slot;",
        "    mgr->way_used = storage->way_used;",
        "    mgr->pool = &storage->pool[0][0];",
        f"    mgr->pool_slots = {pool_slot_count}u;",
        "    mgr->storage_slot_count =",
        "        (uint32_t)(sizeof(storage->slot_map) / sizeof(storage->slot_map[0]));",
        "}",
        "",
        f"#endif /* {guard} */",
        "",
    ]
    write_text(args.image_header, "\n".join(header_lines))

    # --- Descriptor source -------------------------------------------------
    source_lines = [
        "/* Auto-generated by cold_code/tool/gen_cold_code.py. Do not edit. */",
        f'#include "cold_code_{name}_image.h"',
        "",
        f"const cold_code_image_t cold_code_image_{name} = {{",
        f'    .name = "{name}",',
        f"    .image_id = 0x{image_id:08x}u,",
        f"    .slot_size = {args.slot_size}u,",
        f"    .slot_count = {image_slot_count}u,",
        f"    .pool_slot_count = {pool_slot_count}u,",
        "};",
        "",
    ]
    write_text(args.image_source, "\n".join(source_lines))

    # --- Hot stubs and RWX section marker ----------------------------------
    marker = f"cold_code_ram_marker_{name}"
    asm = [
        "/* Auto-generated by cold_code/tool/gen_cold_code.py. Do not edit. */",
        '    .section .cold_ram,"awx",@nobits',
        "    .balign 64",
        f"    .globl {marker}",
        f"    .hidden {marker}",
        f"    .type {marker}, @object",
        f"{marker}:",
        "    .zero 1",
        f"    .size {marker}, . - {marker}",
        "",
        "    .section .text",
    ]

    for fn in globals_:
        fn_name = fn["name"]
        image_slot = fn["offset"] // args.slot_size
        intra_offset = fn["offset"] % args.slot_size
        asm += [
            "    .p2align 4",
            f"    .globl {fn_name}",
            f"    .type {fn_name}, @function",
            f"{fn_name}:",
            "    .byte 0xf3,0x0f,0x1e,0xfa",  # endbr64
            "    pushq %rax",
            "    pushq %rdi",
            "    pushq %rsi",
            "    pushq %rdx",
            "    pushq %rcx",
            "    pushq %r8",
            "    pushq %r9",
            "    subq $128, %rsp",
        ]
        for reg in range(8):
            asm.append(f"    movdqu %xmm{reg}, {reg * 16}(%rsp)")
        asm += [
            f"    movl $0x{image_id:08x}, %edi",
            f"    movl ${image_slot}, %esi",
            "    call cold_code_acquire_slot",
            "    movq %rax, %r11",
        ]
        for reg in range(8):
            asm.append(f"    movdqu {reg * 16}(%rsp), %xmm{reg}")
        asm += [
            "    addq $128, %rsp",
            "    popq %r9",
            "    popq %r8",
            "    popq %rcx",
            "    popq %rdx",
            "    popq %rsi",
            "    popq %rdi",
            "    popq %rax",
            f"    addq ${intra_offset}, %r11",
            "    jmp *%r11",
            f"    .size {fn_name}, . - {fn_name}",
            "",
        ]

    asm += ['    .section .note.GNU-stack,"",@progbits', ""]
    write_text(args.stubs, "\n".join(asm))


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--nm", required=True)
    parser.add_argument("--readelf", required=True)
    parser.add_argument("--objdump", required=True)
    parser.add_argument("--elf", required=True)
    parser.add_argument("--raw-bin", required=True)
    parser.add_argument("--final-bin", required=True)
    parser.add_argument("--image-name", required=True)
    parser.add_argument("--slot-size", required=True, type=int)
    parser.add_argument("--pool-slots", required=True, type=int)
    parser.add_argument("--image-header", required=True)
    parser.add_argument("--image-source", required=True)
    parser.add_argument("--stubs", required=True)
    parser.add_argument("--layout-map", required=True)
    parser.add_argument("--symbols", required=True)
    args = parser.parse_args(argv)

    try:
        generate(args)
    except GenError as exc:
        print(f"gen_cold_code: error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
