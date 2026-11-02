"""Architecture backend registry for the cold-code generator."""

from __future__ import annotations


class ArchError(Exception):
    pass


_INTERFACE_ONLY = {
    "armv7m",
    "aarch64",
    "riscv32",
    "riscv64",
}


def get_backend(name: str):
    # Import lazily so backends can import ArchError from this package.
    from . import x86_64

    if name == x86_64.INFO.name:
        return x86_64
    if name in _INTERFACE_ONLY:
        raise ArchError(
            f"arch backend '{name}' interface only; stub emission not implemented"
        )
    raise ArchError(f"arch backend '{name}' is not implemented")
