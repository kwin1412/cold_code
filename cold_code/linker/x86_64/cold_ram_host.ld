/*
 * Place caller storage and the per-image RWX marker in a dedicated RWX
 * PT_LOAD.  The marker's input flags are "awx" and the inputs are NOBITS, so
 * the kernel maps anonymous zero pages.
 */
SECTIONS
{
  .cold_ram : ALIGN(CONSTANT (MAXPAGESIZE))
  {
    KEEP (*(.cold_ram))
  }
}
INSERT BEFORE .stab;
