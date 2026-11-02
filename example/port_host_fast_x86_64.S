/*
 * Optional FAST_PATH accessor for the host port.
 *
 * Private ABI (see cold_code/arch/x86_64/cold_code_port_fast.h):
 *   returns the current manager in %r11, preserves all argument registers and
 *   xmm0-7, and touches no stack.  The C port keeps the manager in a
 *   local-exec TLS variable; this leaf function only loads it.
 */
    .text
    .p2align 4
    .globl cold_code_port_current_fast
    .hidden cold_code_port_current_fast
    .type cold_code_port_current_fast, @function
cold_code_port_current_fast:
    .byte 0xf3,0x0f,0x1e,0xfa  /* endbr64 */
    mov %fs:cold_code_port_current_manager@tpoff, %r11
    ret
    .size cold_code_port_current_fast, . - cold_code_port_current_fast

    .section .note.GNU-stack,"",@progbits
