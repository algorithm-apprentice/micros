#ifndef MICROS_ARCH_RISCV64_PANIC_CONTEXT_H
#define MICROS_ARCH_RISCV64_PANIC_CONTEXT_H

#define MICROS_PANIC_CONTEXT_SSTATUS_OFFSET 0
#define MICROS_PANIC_CONTEXT_SCAUSE_OFFSET 8
#define MICROS_PANIC_CONTEXT_STVAL_OFFSET 16
#define MICROS_PANIC_CONTEXT_SEPC_OFFSET 24
#define MICROS_PANIC_CONTEXT_RA_OFFSET 32
#define MICROS_PANIC_CONTEXT_SP_OFFSET 40
#define MICROS_PANIC_CONTEXT_SIZE 48

#ifndef __ASSEMBLER__

#include <stddef.h>
#include <stdint.h>

struct micros_panic_machine_context {
    uint64_t sstatus;
    uint64_t scause;
    uint64_t stval;
    uint64_t sepc;
    uint64_t ra;
    uint64_t sp;
};

_Static_assert(
    offsetof(struct micros_panic_machine_context, sstatus)
        == MICROS_PANIC_CONTEXT_SSTATUS_OFFSET,
    "panic sstatus offset mismatch"
);
_Static_assert(
    offsetof(struct micros_panic_machine_context, scause)
        == MICROS_PANIC_CONTEXT_SCAUSE_OFFSET,
    "panic scause offset mismatch"
);
_Static_assert(
    offsetof(struct micros_panic_machine_context, stval)
        == MICROS_PANIC_CONTEXT_STVAL_OFFSET,
    "panic stval offset mismatch"
);
_Static_assert(
    offsetof(struct micros_panic_machine_context, sepc)
        == MICROS_PANIC_CONTEXT_SEPC_OFFSET,
    "panic sepc offset mismatch"
);
_Static_assert(
    offsetof(struct micros_panic_machine_context, ra)
        == MICROS_PANIC_CONTEXT_RA_OFFSET,
    "panic ra offset mismatch"
);
_Static_assert(
    offsetof(struct micros_panic_machine_context, sp)
        == MICROS_PANIC_CONTEXT_SP_OFFSET,
    "panic sp offset mismatch"
);
_Static_assert(
    sizeof(struct micros_panic_machine_context) == MICROS_PANIC_CONTEXT_SIZE,
    "panic context size mismatch"
);

#endif

#endif
