    .section .text.elf_spawn_report, "ax", @progbits
    .balign 4
    .globl micros_elf_spawn_report
    .type micros_elf_spawn_report, @function
    .option push
    .option norelax
    .option norvc
micros_elf_spawn_report:
    ebreak
    ret
    .size micros_elf_spawn_report, . - micros_elf_spawn_report
    .option pop
    .section .note.GNU-stack, "", @progbits
