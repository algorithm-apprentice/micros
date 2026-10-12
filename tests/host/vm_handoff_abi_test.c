#include "vm_handoff_test_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define EXPECT_TRUE(expression) \
    do { \
        if (!(expression)) { \
            fprintf( \
                stderr, \
                "%s:%d: expected %s\n", \
                __FILE__, \
                __LINE__, \
                #expression \
            ); \
            return false; \
        } \
    } while (false)

bool vm_handoff_test_test_decodes_exact_shape(void)
{
    struct micros_syscall_arguments arguments = vm_handoff_test_valid_arguments();
    struct micros_vm_handoff_request request;
    uint32_t command = 0;
    size_t index;

    EXPECT_TRUE(
        micros_vm_handoff_decode_command(&arguments, &command)
            == MICROS_SYSCALL_ABI_OK
        && command == MICROS_VM_HANDOFF_READY
        && micros_vm_handoff_decode(&arguments, &request)
            == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_VM_HANDOFF_READY
        && request.version == MICROS_VM_BOOT_INFO_VERSION
        && request.managed_range_count == 2
        && request.address_space_count == 3
        && request.managed_frame_count == 4096
        && request.free_frame_count == 3000
        && request.mapping_count == 97
        && request.digest == UINT64_C(0x0123456789abcdef)
    );
    arguments.a0 = MICROS_VM_HANDOFF_MAP_TTY_UART;
    EXPECT_TRUE(vm_handoff_test_decode_preserves_output(arguments));
    arguments = vm_handoff_test_valid_arguments();
    arguments.a7 = MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL;
    EXPECT_TRUE(vm_handoff_test_decode_preserves_output(arguments));
    for (index = 0; index <= 5; ++index) {
        arguments = vm_handoff_test_valid_arguments();
        switch (index) {
        case 0:
            arguments.a0 |= UINT64_C(1) << 32;
            break;
        case 1:
            arguments.a1 |= UINT64_C(1) << 32;
            break;
        case 2:
            arguments.a2 |= UINT64_C(1) << 32;
            break;
        case 3:
            arguments.a3 |= UINT64_C(1) << 32;
            break;
        case 4:
            arguments.a4 |= UINT64_C(1) << 32;
            break;
        default:
            arguments.a5 |= UINT64_C(1) << 32;
            break;
        }
        EXPECT_TRUE(vm_handoff_test_decode_preserves_output(arguments));
    }
    return true;
}

bool vm_handoff_test_test_decodes_exact_tty_mapping(void)
{
    struct micros_syscall_arguments arguments =
        vm_handoff_test_valid_tty_mapping_arguments();
    struct micros_vm_tty_mapping_request request;
    uint32_t command = 0;
    size_t index;

    EXPECT_TRUE(
        micros_vm_handoff_decode_command(&arguments, &command)
            == MICROS_SYSCALL_ABI_OK
        && command == MICROS_VM_HANDOFF_MAP_TTY_UART
        && micros_vm_handoff_decode_tty_mapping(&arguments,
            &request) == MICROS_SYSCALL_ABI_OK
        && request.command == MICROS_VM_HANDOFF_MAP_TTY_UART
        && request.version == MICROS_TTY_MAPPING_VERSION
        && request.service_id == MICROS_TTY_SERVICE_ID
        && request.endpoint == UINT32_C(0x00007003)
        && request.virtual_base == MICROS_TTY_UART_VIRTUAL_BASE
        && request.physical_base == MICROS_TTY_UART_PHYSICAL_BASE
        && request.irq_source == MICROS_TTY_UART_IRQ_SOURCE
        && request.mapped_length == MICROS_TTY_UART_MAPPED_LENGTH
        && micros_vm_tty_mapping_matches(&request,
            UINT32_C(0x00007003))
    );
    ++request.version;
    EXPECT_TRUE(
        !micros_vm_tty_mapping_matches(&request,
            UINT32_C(0x00007003))
    );
    --request.version;
    EXPECT_TRUE(
        !micros_vm_tty_mapping_matches(&request,
            UINT32_C(0x00008003))
    );

    arguments.a0 = UINT64_C(10);
    EXPECT_TRUE(vm_handoff_test_tty_mapping_decode_preserves_output(arguments));
    command = UINT32_C(0xa5a5a5a5);
    EXPECT_TRUE(
        micros_vm_handoff_decode_command(&arguments, &command)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && command == UINT32_C(0xa5a5a5a5)
    );
    for (index = 0; index <= 3; ++index) {
        arguments = vm_handoff_test_valid_tty_mapping_arguments();
        switch (index) {
        case 0:
            arguments.a0 |= UINT64_C(1) << 32;
            break;
        case 1:
            arguments.a1 |= UINT64_C(1) << 32;
            break;
        case 2:
            arguments.a2 |= UINT64_C(1) << 32;
            break;
        default:
            arguments.a3 |= UINT64_C(1) << 32;
            break;
        }
        EXPECT_TRUE(vm_handoff_test_tty_mapping_decode_preserves_output(arguments));
    }
    arguments = vm_handoff_test_valid_tty_mapping_arguments();
    arguments.a7 = MICROS_SYSCALL_ABI_BOOTSTRAP_CONTROL;
    EXPECT_TRUE(vm_handoff_test_tty_mapping_decode_preserves_output(arguments));
    return true;
}

static bool decodes_load_begin_command(void)
{
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_VM_LOAD_BEGIN,
        .a1 = UINT64_C(0x1234),
        .a2 = UINT64_C(0x40001000),
        .a3 = MICROS_VM_LOAD_BEGIN_RESULT_SIZE,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    struct micros_vm_load_begin_request begin;
    uint32_t command = 0;

    EXPECT_TRUE(
        micros_vm_handoff_decode_command(&arguments, &command)
            == MICROS_SYSCALL_ABI_OK
        && command == MICROS_VM_LOAD_BEGIN
        && micros_vm_load_begin_decode(&arguments, &begin)
            == MICROS_SYSCALL_ABI_OK
        && begin.process_reservation_token == UINT64_C(0x1234)
        && begin.output_address == UINT64_C(0x40001000)
    );
    return true;
}

static bool decodes_scratch_commands(void)
{
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_VM_SCRATCH_MAP,
        .a1 = UINT64_C(0x10),
        .a2 = MICROS_USER_VIRTUAL_BASE,
        .a3 = UINT64_C(0x00800000),
        .a4 = MICROS_SV39_PERMISSION_USER
            | MICROS_SV39_PERMISSION_READ
            | MICROS_SV39_PERMISSION_EXECUTE,
        .a5 = MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    struct micros_vm_scratch_map_request map;
    struct micros_vm_scratch_commit_request commit;

    EXPECT_TRUE(
        micros_vm_scratch_map_decode(&arguments, &map) == MICROS_SYSCALL_ABI_OK
        && map.load_token == UINT64_C(0x10)
        && map.virtual_address == MICROS_USER_VIRTUAL_BASE
        && map.physical_address == UINT64_C(0x00800000)
        && map.permissions
            == (MICROS_SV39_PERMISSION_USER
                | MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_EXECUTE)
    );

    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_VM_SCRATCH_COMMIT,
        .a1 = UINT64_C(0x10),
        .a2 = MICROS_USER_VIRTUAL_BASE,
        .a3 = UINT64_C(0x00800000),
        .a4 = MICROS_VM_SCRATCH_VIRTUAL_ADDRESS,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    EXPECT_TRUE(
        micros_vm_scratch_commit_decode(&arguments, &commit)
            == MICROS_SYSCALL_ABI_OK
        && commit.load_token == UINT64_C(0x10)
        && commit.virtual_address == MICROS_USER_VIRTUAL_BASE
        && commit.physical_address == UINT64_C(0x00800000)
    );
    return true;
}

static bool decodes_freeze_command(void)
{
    const struct micros_syscall_arguments arguments = {
        .a0 = MICROS_VM_LOAD_FREEZE,
        .a1 = UINT64_C(0x40002000),
        .a2 = MICROS_VM_LOAD_FREEZE_PLAN_SIZE,
        .a3 = UINT64_C(0x40003000),
        .a4 = MICROS_VM_LOAD_FREEZE_RESULT_SIZE,
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    struct micros_vm_load_freeze_request freeze;

    EXPECT_TRUE(
        micros_vm_load_freeze_decode(&arguments, &freeze)
            == MICROS_SYSCALL_ABI_OK
        && freeze.plan_address == UINT64_C(0x40002000)
        && freeze.output_address == UINT64_C(0x40003000)
    );
    return true;
}

static bool decodes_abort_commit_and_exit_commands(void)
{
    struct micros_syscall_arguments arguments = {
        .a0 = MICROS_VM_LOAD_ABORT,
        .a1 = UINT64_C(0x10),
        .a2 = UINT64_C(0x20),
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    struct micros_vm_load_abort_request abort_request;
    struct micros_vm_load_commit_request load_commit;
    struct micros_vm_exit_release_request release;
    struct micros_vm_exit_release_request release_sentinel;

    EXPECT_TRUE(
        micros_vm_load_abort_decode(&arguments, &abort_request)
            == MICROS_SYSCALL_ABI_OK
        && abort_request.load_token == UINT64_C(0x10)
        && abort_request.mapping_generation == UINT64_C(0x20)
    );
    arguments.a0 = MICROS_VM_LOAD_COMMIT;
    EXPECT_TRUE(
        micros_vm_load_commit_decode(&arguments, &load_commit)
            == MICROS_SYSCALL_ABI_OK
        && load_commit.load_token == UINT64_C(0x10)
        && load_commit.mapping_generation == UINT64_C(0x20)
    );
    arguments = (struct micros_syscall_arguments){
        .a0 = MICROS_VM_EXIT_RELEASE,
        .a1 = UINT64_C(0x30),
        .a2 = UINT64_C(0x10),
        .a3 = UINT64_C(0x20),
        .a7 = MICROS_SYSCALL_ABI_VM_HANDOFF,
    };
    EXPECT_TRUE(
        micros_vm_exit_release_decode(&arguments, &release)
            == MICROS_SYSCALL_ABI_OK
        && release.exit_token == UINT64_C(0x30)
        && release.load_token == UINT64_C(0x10)
        && release.mapping_generation == UINT64_C(0x20)
    );

    arguments.a4 = 1;
    memset(&release_sentinel, 0xa5, sizeof(release_sentinel));
    release = release_sentinel;
    EXPECT_TRUE(
        micros_vm_exit_release_decode(&arguments, &release)
            == MICROS_SYSCALL_ABI_ARGUMENT
        && memcmp(&release, &release_sentinel, sizeof(release)) == 0
    );
    return true;
}

bool vm_handoff_test_test_decodes_load_commands(void)
{
    return (
        decodes_load_begin_command()
        && decodes_scratch_commands()
        && decodes_freeze_command()
        && decodes_abort_commit_and_exit_commands()
    );
}
