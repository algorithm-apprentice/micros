#ifndef MICROS_SELECTED_IPC_WORK_H
#define MICROS_SELECTED_IPC_WORK_H

#if defined(MICROS_BUILD_IPC_SYSCALL_TEST)

#if !defined(MICROS_TEST_WORK_COUNTS) || MICROS_TEST_WORK_COUNTS != 1
#error "the IPC syscall work fixture requires its test-owned collector"
#endif

#include "micros/kernel_objects.h"

void micros_ipc01_syscall_begin(uint64_t operation, struct micros_thread_handle thread);
void micros_ipc01_syscall_end(bool accepted);
void micros_ipc01_completion_begin(struct micros_thread_handle thread);
void micros_ipc01_completion_end(bool accepted, uint64_t result, size_t chunks, uint64_t token);
bool micros_ipc01_work_report(void);

#define MICROS_IPC01_SYSCALL_BEGIN(operation, thread) \
    micros_ipc01_syscall_begin((operation), (thread))
#define MICROS_IPC01_SYSCALL_END(accepted) micros_ipc01_syscall_end((accepted))
#define MICROS_IPC01_COMPLETION_BEGIN(thread) micros_ipc01_completion_begin((thread))
#define MICROS_IPC01_COMPLETION_END(accepted, result, chunks, token) \
    micros_ipc01_completion_end((accepted), (result), (chunks), (token))

#else

#define MICROS_IPC01_SYSCALL_BEGIN(operation, thread) ((void)0)
#define MICROS_IPC01_SYSCALL_END(accepted) ((void)0)
#define MICROS_IPC01_COMPLETION_BEGIN(thread) ((void)0)
#define MICROS_IPC01_COMPLETION_END(accepted, result, chunks, token) ((void)0)

#endif

#endif
