/*
 * FAST_PATH accessor for the custom per-CPU port test.  Same private ABI as
 * the host reference: current manager in %r11, argument registers preserved.
 */
    .text
    .p2align 4
    .globl cold_code_port_current_fast
    .hidden cold_code_port_current_fast
    .type cold_code_port_current_fast, @function
cold_code_port_current_fast:
    .byte 0xf3,0x0f,0x1e,0xfa  /* endbr64 */
    mov %fs:cold_code_port_current_manager_fast@tpoff, %r11
    ret
    .size cold_code_port_current_fast, . - cold_code_port_current_fast

    .section .note.GNU-stack,"",@progbits
