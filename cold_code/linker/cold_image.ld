/*
 * Flat cold-code image: linked at VMA 0 and copied into RAM slots at runtime.
 *
 * Only functions annotated with COLD_CODE() (section .cold_text) are kept.
 * Any reference to data, libc or another section is therefore a link error,
 * which enforces the self-contained cold-function contract.
 */
SECTIONS
{
  . = 0;
  .cold_text : {
    *(.cold_text)
    *(.cold_text.*)
  }

  /DISCARD/ : {
    *(.text) *(.text.*)
    *(.rodata) *(.rodata.*) *(.data.rel.ro*)
    *(.data) *(.data.*) *(.sdata*) *(.ldata*)
    *(.bss) *(.bss.*) *(.sbss*) *(.lbss*) *(.tbss) *(.tdata*)
    *(.got) *(.got.plt) *(.plt) *(.plt.got) *(.plt.sec)
    *(.dynamic) *(.dynsym) *(.dynstr) *(.interp) *(.hash) *(.gnu.*)
    *(.init_array*) *(.fini_array*) *(.init) *(.fini)
    *(.comment) *(.note*) *(.eh_frame*) *(.sframe*) *(.gcc_except_table*)
  }
}
