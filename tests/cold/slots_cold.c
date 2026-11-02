#include <stdint.h>

#include "cold_code.h"

/*
 * Returns a runtime address inside this function's copied slot.  The test uses
 * it to prove that two managers load the same image into different physical
 * slots.  This is x86-64-specific, like the generated stubs.
 */
COLD_CODE()
uintptr_t cold_slot_address(void) {
    uintptr_t address;
    __asm__ volatile("lea 0(%%rip), %0" : "=r"(address));
    return address;
}
