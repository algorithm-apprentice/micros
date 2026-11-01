.option push
.option norelax
.option norvc
.section .text.__cyg_profile_func_enter, "ax", @progbits
.balign 4
.globl __cyg_profile_func_enter
.type __cyg_profile_func_enter, @function
__cyg_profile_func_enter:
    andi t0, sp, 15
    bnez t0, .Lbad
#ifdef MICROS_STACK_OBSERVER_KERNEL
    la t0, __boot_stack_bottom
    bltu sp, t0, .Lprimary
    la t1, __boot_stack_top
    bgeu sp, t1, .Lprimary
    li t2, 0
    li t4, 0
    j .Lsample
.Lprimary:
    la t0, __trap_stack_bottom
    bltu sp, t0, .Lemergency
    la t1, __trap_stack_top
    bgeu sp, t1, .Lemergency
    li t2, 1
    li t4, 0
    j .Lsample
.Lemergency:
    la t0, __trap_emergency_stack_bottom
    bltu sp, t0, .Lthread
    la t1, __trap_emergency_stack_top
    bgeu sp, t1, .Lthread
    li t2, 2
    li t4, 0
    j .Lsample
.Lthread:
    la t0, __thread_kernel_stacks_start
    bltu sp, t0, .Lbad
    la t1, __thread_kernel_stacks_end
    bgeu sp, t1, .Lbad
    sub t2, sp, t0
    srli t2, t2, 14
    slli t1, t2, 14
    add t0, t0, t1
    bltu sp, t0, .Lbad
    li t1, 16384
    add t1, t0, t1
    bltu t1, sp, .Lbad
    addi t2, t2, 3
    li t4, 1
.Lsample:
    sub t5, t1, sp
    slli t2, t2, 6
    la t3, micros_stack_observations
    add t3, t3, t2
    ld t0, 0(t3)
    beqz t4, .Lstatic_owner
    beqz t0, .Lbad
    j .Lowner_ready
.Lstatic_owner:
    bnez t0, .Lbad
.Lowner_ready:
    ld t0, 32(t3)
    bnez t0, .Lbad
#else
    li t0, 0x7ffff000
    bltu sp, t0, .Lbad
    li t1, 0x80000000
    bltu t1, sp, .Lbad
    sub t5, t1, sp
    la t3, micros_stack_user_observation
#endif
    addi t0, t3, 8
    amomaxu.d.aqrl zero, t5, (t0)
    addi t0, t3, 16
    li t1, 1
    amoadd.d.aqrl t2, t1, (t0)
    li t1, -1
    bne t2, t1, .Lreturn
    addi t0, t3, 24
    li t1, 1
    amoor.d.aqrl zero, t1, (t0)
.Lreturn:
    ret
.Lbad:
#ifdef MICROS_STACK_OBSERVER_KERNEL
    la t0, micros_stack_observer_failure
#else
    la t0, micros_stack_user_observation
    addi t0, t0, 24
#endif
    li t1, 1
    amoor.d.aqrl zero, t1, (t0)
    ret
.size __cyg_profile_func_enter, . - __cyg_profile_func_enter
.section .text.__cyg_profile_func_exit, "ax", @progbits
.balign 4
.globl __cyg_profile_func_exit
.type __cyg_profile_func_exit, @function
__cyg_profile_func_exit:
    ret
.size __cyg_profile_func_exit, . - __cyg_profile_func_exit
.section .bss.stack_observation, "aw", @nobits
.balign 8
#ifdef MICROS_STACK_OBSERVER_KERNEL
.globl micros_stack_observations
.type micros_stack_observations, @object
micros_stack_observations:
    .skip 131 * 64
.size micros_stack_observations, . - micros_stack_observations
.globl micros_stack_observer_failure
.type micros_stack_observer_failure, @object
micros_stack_observer_failure:
    .skip 8
.size micros_stack_observer_failure, . - micros_stack_observer_failure
#else
.globl micros_stack_user_observation
.type micros_stack_user_observation, @object
micros_stack_user_observation:
    .skip 64
.size micros_stack_user_observation, . - micros_stack_user_observation
#endif
.option pop
.section .note.GNU-stack, "", @progbits
