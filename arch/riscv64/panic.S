#include "arch/riscv64/panic_context.h"

.section .text, "ax", @progbits
.globl micros_panic_entry
.type micros_panic_entry, @function
micros_panic_entry:
    csrrci t0, sstatus, 2
    addi sp, sp, -MICROS_PANIC_CONTEXT_SIZE
    sd t0, MICROS_PANIC_CONTEXT_SSTATUS_OFFSET(sp)

    csrr t0, scause
    sd t0, MICROS_PANIC_CONTEXT_SCAUSE_OFFSET(sp)
    csrr t0, stval
    sd t0, MICROS_PANIC_CONTEXT_STVAL_OFFSET(sp)
    csrr t0, sepc
    sd t0, MICROS_PANIC_CONTEXT_SEPC_OFFSET(sp)

    sd ra, MICROS_PANIC_CONTEXT_RA_OFFSET(sp)
    addi t0, sp, MICROS_PANIC_CONTEXT_SIZE
    sd t0, MICROS_PANIC_CONTEXT_SP_OFFSET(sp)

    mv a4, sp
    call micros_panic_report
1:
    wfi
    j 1b
.size micros_panic_entry, . - micros_panic_entry
