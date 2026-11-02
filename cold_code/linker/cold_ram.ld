/*
 * Place the generated .cold_ram sink after DATA_SEGMENT_END so GNU ld creates
 * a dedicated RWX PT_LOAD for it (the section's input flags are "awx" and it
 * is NOBITS, so p_filesz is 0 and the kernel maps anonymous zero pages).
 */
SECTIONS
{
  .cold_ram : ALIGN(CONSTANT (MAXPAGESIZE))
  {
    KEEP (*(.cold_ram))
  }
}
INSERT BEFORE .stab;
