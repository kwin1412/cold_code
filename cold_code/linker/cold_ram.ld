/*
 * Place the caller's generated storage and the per-image RWX marker after
 * DATA_SEGMENT_END so GNU ld creates a dedicated RWX PT_LOAD for them (the
 * marker's input flags are "awx" and both inputs are NOBITS, so p_filesz is 0
 * and the kernel maps anonymous zero pages).
 */
SECTIONS
{
  .cold_ram : ALIGN(CONSTANT (MAXPAGESIZE))
  {
    KEEP (*(.cold_ram))
  }
}
INSERT BEFORE .stab;
