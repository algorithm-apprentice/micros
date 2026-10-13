#include "tests/host/spawn_abi_test_internal.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "micros/sv39.h"

int main(void)
{
    struct spawn_abi_test_context *context = spawn_abi_test_context_get();
    if (
        !test_spawn_request_contract(context)
        || !test_spawn_request_boundaries(context)
        || !test_spawn_request_rejections(context)
        || !test_application_stack(context)
        || !test_application_stack_boundaries(context)
        || !test_elf_plan(context)
        || !test_elf_header_rejections(context)
        || !test_elf_segment_rejections(context)
    ) {
        return 1;
    }
    printf("spawn ABI tests passed\n");
    return 0;
}
