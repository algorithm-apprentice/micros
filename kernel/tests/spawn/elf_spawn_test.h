#ifndef MICROS_KERNEL_ELF_SPAWN_TEST_H
#define MICROS_KERNEL_ELF_SPAWN_TEST_H

#include <stdbool.h>

struct micros_hart;
struct micros_trap_frame;

_Noreturn void micros_elf_spawn_test_launch(void);
bool micros_elf_spawn_test_handle_trap(
    struct micros_hart *hart, struct micros_trap_frame *frame
);

#endif
