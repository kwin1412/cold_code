/*
 * x86-64 fast accessor for the host port.
 *
 * Private ABI: returns the current manager in %r11, preserves rax/AL, rdi,
 * rsi, rdx, rcx, r8, r9, xmm0-7; may clobber r10, r11 and flags; no stack.
 */
    .text
    .p2align 4
    .globl cold_code_context_current_fast
    .hidden cold_code_context_current_fast
    .type cold_code_context_current_fast, @function
cold_code_context_current_fast:
    .byte 0xf3,0x0f,0x1e,0xfa  /* endbr64 */
    mov %fs:cold_code_host_current_manager@tpoff, %r11
    ret
    .size cold_code_context_current_fast, . - cold_code_context_current_fast

    .section .note.GNU-stack,"",@progbits
