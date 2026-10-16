#include "micros/kernel_objects.h"
#include "micros/scheduler_core.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "kernel_objects_test_internal.h"

int main(void)
{
    const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {
            "initialization is checked and one shot",
            kernel_object_test_initialization_is_checked_and_one_shot,
        },
        {
            "uninitialized registry rejects every operation",
            kernel_object_test_uninitialized_registry_rejects_every_operation,
        },
        {
            "fixed capacities report exhaustion atomically",
            kernel_object_test_fixed_capacities_report_exhaustion_atomically,
        },
        {
            "process slots reuse with new generations",
            kernel_object_test_process_slots_reuse_with_new_generations,
        },
        {
            "process exact-slot creation is isolated",
            kernel_object_test_process_exact_slot_creation_is_isolated,
        },
        {
            "process address-space lifecycle is exact",
            kernel_object_test_process_address_space_lifecycle_is_exact,
        },
        {
            "thread execution-context lifecycle is exact",
            kernel_object_test_thread_execution_context_lifecycle_is_exact,
        },
        {
            "thread slots reuse with new generations",
            kernel_object_test_thread_slots_reuse_with_new_generations,
        },
        {
            "thread ownership and production limit",
            kernel_object_test_thread_ownership_and_production_limit,
        },
        {
            "model policy supports multiple threads",
            kernel_object_test_model_policy_supports_multiple_threads,
        },
        {
            "hart policy and stack validation are atomic",
            kernel_object_test_hart_policy_and_stack_validation_are_atomic,
        },
        {
            "generation boundaries and quarantine",
            kernel_object_test_generation_boundaries_and_quarantine,
        },
        {
            "invalid arguments preserve outputs",
            kernel_object_test_invalid_arguments_and_stale_handles_preserve_outputs,
        },
        {
            "seeded lifecycle model",
            kernel_object_test_seeded_lifecycle_model,
        },
        {
            "validator rejects corrupt relationships",
            kernel_object_test_validator_rejects_corrupt_relationships,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(stderr, "not ok %zu - %s\n", index + 1, tests[index].name);
            return 1;
        }
        printf("ok %zu - %s\n", index + 1, tests[index].name);
    }
    return 0;
}
