.section .text, "ax", @progbits
.balign 4
.globl micros_riscv_activate_sv39
.type micros_riscv_activate_sv39, @function
micros_riscv_activate_sv39:
    sfence.vma x0, x0
    srli a0, a0, 12
    li t0, 8
    slli t0, t0, 60
    or a0, a0, t0
    csrw satp, a0
    sfence.vma x0, x0
    csrr a0, satp
    ret
.size micros_riscv_activate_sv39, . - micros_riscv_activate_sv39

.balign 4
.globl micros_riscv_publish_page_table
.type micros_riscv_publish_page_table, @function
micros_riscv_publish_page_table:
    fence rw, rw
    sfence.vma x0, x0
    ret
.size micros_riscv_publish_page_table, . - micros_riscv_publish_page_table
