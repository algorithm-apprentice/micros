#ifndef MICROS_SELECTED_GRANT_VALIDATION_INTERNAL_H
#define MICROS_SELECTED_GRANT_VALIDATION_INTERNAL_H

#include "arch/riscv64/trap_context.h"
#include "kernel/tty/tty_handoff_runtime_internal.h"
#include "micros/frame_ownership.h"
#include "micros/grant_copy.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

enum { GRANT01_ACTORS = 4, GRANT01_MEMORY_SIZE = 8192 };

struct grant01_fixture {
    struct micros_kernel_objects objects;
    struct micros_kernel_objects before_objects;
    struct micros_endpoint_registry endpoints;
    struct micros_endpoint_registry before_endpoints;
    struct micros_grant_registry grants;
    struct micros_grant_registry before_grants;
    struct micros_process_handle processes[GRANT01_ACTORS];
    struct micros_thread_handle threads[GRANT01_ACTORS];
    micros_endpoint_t identities[GRANT01_ACTORS];
    struct micros_hart_handle hart;
    struct micros_frame_ownership ledger;
    struct micros_bootstrap_control_state bootstrap;
    struct tty_handoff_context before_tty;
    struct micros_trap_frame frame;
    jmp_buf panic_jump;
    const char *panic_reason;
    bool panic_armed;
    bool ledger_available;
    uintptr_t irq_status;
    uintptr_t memory_base[GRANT01_ACTORS];
    enum micros_user_address_space_error translation_error[GRANT01_ACTORS][2];
    uint32_t permissions[GRANT01_ACTORS][2];
    enum micros_user_address_space_error device_error;
    size_t translations[GRANT01_ACTORS];
    size_t device_checks;
    _Alignas(4096) unsigned char memory[GRANT01_ACTORS][GRANT01_MEMORY_SIZE];
    unsigned char before_memory[GRANT01_ACTORS][GRANT01_MEMORY_SIZE];
};

#define REQUIRE(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "GRANT01_FIXTURE_FAILURE file=%s line=%d expression=%s\n", \
                    __FILE__, __LINE__, #condition); \
            return false; \
        } \
    } while (false)

struct grant01_fixture *grant01_fixture(void);
bool grant01_setup(size_t occupied, size_t actors);
void grant01_snapshot(void);
bool grant01_begin(size_t index);
bool grant01_finish(void);
bool grant01_report(size_t index, const char *name, enum micros_grant_error result,
                    uint64_t value, bool preserved);
bool grant01_runtime_cases(size_t *count);
bool grant01_scope_cases(size_t *count);

#endif
