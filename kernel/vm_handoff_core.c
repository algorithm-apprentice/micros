#include "kernel/vm_handoff_core.h"

#include <stddef.h>
#include <stdint.h>

#include "micros/bootstrap.h"
#include "micros/endpoint.h"
#include "micros/frame_allocator.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"

#define MICROS_VM_HANDOFF_MAGIC UINT64_C(0x4d4943524f535648)
#define MICROS_VM_PAGE_DIGEST_OFFSET UINT64_C(14695981039346656037)
#define MICROS_VM_PAGE_DIGEST_PRIME UINT64_C(1099511628211)

static bool bytes_are_zero(const void *storage, size_t size) {
    const unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static void clear_bytes(void *storage, size_t size) {
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size) {
    unsigned char *output = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        output[index] = input[index];
    }
}

static bool fits_u32(uint64_t value) {
    return value <= UINT32_MAX;
}

static bool vm_handoff_command_is_known(uint32_t command) {
    return (command == MICROS_VM_HANDOFF_READY || command == MICROS_VM_HANDOFF_MAP_TTY_UART ||
            command == MICROS_VM_LOAD_BEGIN || command == MICROS_VM_SCRATCH_MAP ||
            command == MICROS_VM_SCRATCH_COMMIT || command == MICROS_VM_LOAD_FREEZE ||
            command == MICROS_VM_LOAD_ABORT || command == MICROS_VM_LOAD_COMMIT ||
            command == MICROS_VM_EXIT_RELEASE);
}

static bool pointer_is_aligned(const void *pointer, size_t alignment) {
    return (uintptr_t)pointer % alignment == 0;
}

static bool process_is_valid(struct micros_process_handle process) {
    return (process.slot < MICROS_PROCESS_CAPACITY && process.generation != 0 &&
            process.generation <= MICROS_PROCESS_GENERATION_MAX);
}

static bool thread_is_valid(struct micros_thread_handle thread) {
    return (thread.slot < MICROS_THREAD_CAPACITY && thread.generation != 0);
}

static bool process_handles_equal(struct micros_process_handle left,
                                  struct micros_process_handle right) {
    return (left.slot == right.slot && left.generation == right.generation);
}

static bool address_is_page_aligned(uint64_t address) {
    return address != 0 && address % MICROS_FRAME_SIZE == 0;
}

static bool user_page_is_valid(uint64_t virtual_address) {
    return (virtual_address >= MICROS_USER_VIRTUAL_BASE &&
            virtual_address < MICROS_USER_VIRTUAL_END && virtual_address % MICROS_FRAME_SIZE == 0);
}

static bool user_permissions_are_valid(uint32_t permissions) {
    const uint32_t defined = MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE |
                             MICROS_SV39_PERMISSION_EXECUTE | MICROS_SV39_PERMISSION_USER;

    return ((permissions & ~defined) == 0 && (permissions & MICROS_SV39_PERMISSION_USER) != 0 &&
            (permissions & MICROS_SV39_PERMISSION_READ) != 0 &&
            ((permissions & MICROS_SV39_PERMISSION_WRITE) == 0 ||
             ((permissions & MICROS_SV39_PERMISSION_EXECUTE) == 0)));
}

static bool mapping_entry_is_valid(const struct micros_vm_kernel_mapping_entry *entry) {
    return (entry->reserved == 0 && user_page_is_valid(entry->virtual_address) &&
            entry->virtual_address != MICROS_VM_SCRATCH_VIRTUAL_ADDRESS &&
            address_is_page_aligned(entry->physical_address) &&
            user_permissions_are_valid(entry->permissions));
}

static bool stack_pointer_is_valid(uint64_t stack_pointer) {
    return (stack_pointer >= MICROS_APPLICATION_STACK_TOP - MICROS_APPLICATION_STACK_OCCUPIED_MAX &&
            stack_pointer < MICROS_APPLICATION_STACK_TOP && stack_pointer % 16 == 0);
}

static uint64_t next_nonzero_token(uint64_t previous) {
    return previous == UINT64_MAX ? 0 : previous + 1;
}

static bool endpoint_matches_process(uint32_t endpoint, struct micros_process_handle process) {
    return (process_is_valid(process) &&
            endpoint == ((process.generation << MICROS_ENDPOINT_SLOT_BITS) | process.slot) &&
            endpoint != MICROS_ENDPOINT_NONE && endpoint != MICROS_ENDPOINT_ANY);
}

static bool summary_is_valid(const struct micros_vm_boot_summary *summary) {
    return (summary->version == MICROS_VM_BOOT_INFO_VERSION && summary->managed_range_count != 0 &&
            summary->managed_range_count <= MICROS_VM_MAX_MANAGED_RANGES &&
            summary->address_space_count != 0 &&
            summary->address_space_count <= MICROS_VM_MAX_STATIC_ADDRESS_SPACES &&
            summary->mapping_count != 0 &&
            summary->mapping_count <= MICROS_VM_MAX_STATIC_MAPPINGS &&
            summary->managed_frame_count != 0 &&
            summary->managed_frame_count <= MICROS_VM_MAX_MANAGED_FRAMES &&
            summary->free_frame_count <= summary->managed_frame_count &&
            summary->vm_self_wired_frame_count != 0 &&
            summary->vm_self_wired_frame_count <= summary->managed_frame_count);
}

enum micros_syscall_abi_result
micros_vm_handoff_decode(const struct micros_syscall_arguments *arguments,
                         struct micros_vm_handoff_request *request) {
    struct micros_vm_handoff_request candidate;
    uint32_t packed_counts;
    uint32_t command;

    if (arguments == NULL || request == NULL ||
        !pointer_is_aligned(arguments, _Alignof(struct micros_syscall_arguments)) ||
        !pointer_is_aligned(request, _Alignof(struct micros_vm_handoff_request))) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    if (micros_vm_handoff_decode_command(arguments, &command) != MICROS_SYSCALL_ABI_OK ||
        !fits_u32(arguments->a1) || !fits_u32(arguments->a2) || !fits_u32(arguments->a3) ||
        !fits_u32(arguments->a4) || !fits_u32(arguments->a5) ||
        command != MICROS_VM_HANDOFF_READY) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    packed_counts = (uint32_t)arguments->a2;
    clear_bytes(&candidate, sizeof(candidate));
    candidate.command = (uint32_t)arguments->a0;
    candidate.version = (uint32_t)arguments->a1;
    candidate.managed_range_count = (uint16_t)packed_counts;
    candidate.address_space_count = (uint16_t)(packed_counts >> 16);
    candidate.managed_frame_count = (uint32_t)arguments->a3;
    candidate.free_frame_count = (uint32_t)arguments->a4;
    candidate.mapping_count = (uint32_t)arguments->a5;
    candidate.digest = arguments->a6;
    copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result
micros_vm_handoff_decode_command(const struct micros_syscall_arguments *arguments,
                                 uint32_t *command) {
    uint32_t candidate;

    if (arguments == NULL || command == NULL ||
        !pointer_is_aligned(arguments, _Alignof(struct micros_syscall_arguments)) ||
        !pointer_is_aligned(command, _Alignof(uint32_t)) ||
        arguments->a7 != MICROS_SYSCALL_ABI_VM_HANDOFF || !fits_u32(arguments->a0) ||
        !vm_handoff_command_is_known((uint32_t)arguments->a0)) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    candidate = (uint32_t)arguments->a0;
    copy_bytes(command, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result
micros_vm_handoff_decode_tty_mapping(const struct micros_syscall_arguments *arguments,
                                     struct micros_vm_tty_mapping_request *request) {
    struct micros_vm_tty_mapping_request candidate;
    uint32_t command;
    uint32_t irq_source;
    uint32_t mapped_length;

    if (arguments == NULL || request == NULL ||
        !pointer_is_aligned(arguments, _Alignof(struct micros_syscall_arguments)) ||
        !pointer_is_aligned(request, _Alignof(struct micros_vm_tty_mapping_request)) ||
        micros_vm_handoff_decode_command(arguments, &command) != MICROS_SYSCALL_ABI_OK ||
        command != MICROS_VM_HANDOFF_MAP_TTY_UART || !fits_u32(arguments->a1) ||
        !fits_u32(arguments->a2) || !fits_u32(arguments->a3)) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    irq_source = (uint32_t)(arguments->a6 >> 32);
    mapped_length = (uint32_t)arguments->a6;
    if (irq_source == 0 || mapped_length == 0) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }

    clear_bytes(&candidate, sizeof(candidate));
    candidate.command = command;
    candidate.version = (uint32_t)arguments->a1;
    candidate.service_id = (uint32_t)arguments->a2;
    candidate.endpoint = (uint32_t)arguments->a3;
    candidate.virtual_base = arguments->a4;
    candidate.physical_base = arguments->a5;
    candidate.irq_source = irq_source;
    candidate.mapped_length = mapped_length;
    copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result
micros_vm_load_begin_decode(const struct micros_syscall_arguments *arguments,
                            struct micros_vm_load_begin_request *request) {
    struct micros_vm_load_begin_request candidate;
    uint32_t command;

    if (arguments == NULL || request == NULL ||
        !pointer_is_aligned(arguments, _Alignof(struct micros_syscall_arguments)) ||
        !pointer_is_aligned(request, _Alignof(struct micros_vm_load_begin_request)) ||
        micros_vm_handoff_decode_command(arguments, &command) != MICROS_SYSCALL_ABI_OK ||
        command != MICROS_VM_LOAD_BEGIN || arguments->a1 == 0 || arguments->a2 == 0 ||
        arguments->a2 % 8 != 0 || arguments->a3 != MICROS_VM_LOAD_BEGIN_RESULT_SIZE ||
        arguments->a4 != 0 || arguments->a5 != 0 || arguments->a6 != 0) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    candidate.process_reservation_token = arguments->a1;
    candidate.output_address = arguments->a2;
    copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result
micros_vm_scratch_map_decode(const struct micros_syscall_arguments *arguments,
                             struct micros_vm_scratch_map_request *request) {
    struct micros_vm_scratch_map_request candidate;
    uint32_t command;

    if (arguments == NULL || request == NULL ||
        !pointer_is_aligned(arguments, _Alignof(struct micros_syscall_arguments)) ||
        !pointer_is_aligned(request, _Alignof(struct micros_vm_scratch_map_request)) ||
        micros_vm_handoff_decode_command(arguments, &command) != MICROS_SYSCALL_ABI_OK ||
        command != MICROS_VM_SCRATCH_MAP || arguments->a1 == 0 ||
        !user_page_is_valid(arguments->a2) || arguments->a2 == MICROS_VM_SCRATCH_VIRTUAL_ADDRESS ||
        !address_is_page_aligned(arguments->a3) || !fits_u32(arguments->a4) ||
        !user_permissions_are_valid((uint32_t)arguments->a4) ||
        arguments->a5 != MICROS_VM_SCRATCH_VIRTUAL_ADDRESS || arguments->a6 != 0) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    candidate.load_token = arguments->a1;
    candidate.virtual_address = arguments->a2;
    candidate.physical_address = arguments->a3;
    candidate.permissions = (uint32_t)arguments->a4;
    copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result
micros_vm_scratch_commit_decode(const struct micros_syscall_arguments *arguments,
                                struct micros_vm_scratch_commit_request *request) {
    struct micros_vm_scratch_commit_request candidate;
    uint32_t command;

    if (arguments == NULL || request == NULL ||
        !pointer_is_aligned(arguments, _Alignof(struct micros_syscall_arguments)) ||
        !pointer_is_aligned(request, _Alignof(struct micros_vm_scratch_commit_request)) ||
        micros_vm_handoff_decode_command(arguments, &command) != MICROS_SYSCALL_ABI_OK ||
        command != MICROS_VM_SCRATCH_COMMIT || arguments->a1 == 0 ||
        !user_page_is_valid(arguments->a2) || arguments->a2 == MICROS_VM_SCRATCH_VIRTUAL_ADDRESS ||
        !address_is_page_aligned(arguments->a3) ||
        arguments->a4 != MICROS_VM_SCRATCH_VIRTUAL_ADDRESS || arguments->a5 != 0 ||
        arguments->a6 != 0) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    candidate.load_token = arguments->a1;
    candidate.virtual_address = arguments->a2;
    candidate.physical_address = arguments->a3;
    copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result
micros_vm_load_freeze_decode(const struct micros_syscall_arguments *arguments,
                             struct micros_vm_load_freeze_request *request) {
    struct micros_vm_load_freeze_request candidate;
    uint32_t command;

    if (arguments == NULL || request == NULL ||
        !pointer_is_aligned(arguments, _Alignof(struct micros_syscall_arguments)) ||
        !pointer_is_aligned(request, _Alignof(struct micros_vm_load_freeze_request)) ||
        micros_vm_handoff_decode_command(arguments, &command) != MICROS_SYSCALL_ABI_OK ||
        command != MICROS_VM_LOAD_FREEZE || arguments->a1 == 0 || arguments->a1 % 8 != 0 ||
        arguments->a2 != MICROS_VM_LOAD_FREEZE_PLAN_SIZE || arguments->a3 == 0 ||
        arguments->a3 % 8 != 0 || arguments->a4 != MICROS_VM_LOAD_FREEZE_RESULT_SIZE ||
        arguments->a5 != 0 || arguments->a6 != 0) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    candidate.plan_address = arguments->a1;
    candidate.output_address = arguments->a3;
    copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result
micros_vm_load_abort_decode(const struct micros_syscall_arguments *arguments,
                            struct micros_vm_load_abort_request *request) {
    struct micros_vm_load_abort_request candidate;
    uint32_t command;

    if (arguments == NULL || request == NULL ||
        !pointer_is_aligned(arguments, _Alignof(struct micros_syscall_arguments)) ||
        !pointer_is_aligned(request, _Alignof(struct micros_vm_load_abort_request)) ||
        micros_vm_handoff_decode_command(arguments, &command) != MICROS_SYSCALL_ABI_OK ||
        command != MICROS_VM_LOAD_ABORT || arguments->a1 == 0 || arguments->a3 != 0 ||
        arguments->a4 != 0 || arguments->a5 != 0 || arguments->a6 != 0) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    candidate.load_token = arguments->a1;
    candidate.mapping_generation = arguments->a2;
    copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result
micros_vm_load_commit_decode(const struct micros_syscall_arguments *arguments,
                             struct micros_vm_load_commit_request *request) {
    struct micros_vm_load_commit_request candidate;
    uint32_t command;

    if (arguments == NULL || request == NULL ||
        !pointer_is_aligned(arguments, _Alignof(struct micros_syscall_arguments)) ||
        !pointer_is_aligned(request, _Alignof(struct micros_vm_load_commit_request)) ||
        micros_vm_handoff_decode_command(arguments, &command) != MICROS_SYSCALL_ABI_OK ||
        command != MICROS_VM_LOAD_COMMIT || arguments->a1 == 0 || arguments->a2 == 0 ||
        arguments->a3 != 0 || arguments->a4 != 0 || arguments->a5 != 0 || arguments->a6 != 0) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    candidate.load_token = arguments->a1;
    candidate.mapping_generation = arguments->a2;
    copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

enum micros_syscall_abi_result
micros_vm_exit_release_decode(const struct micros_syscall_arguments *arguments,
                              struct micros_vm_exit_release_request *request) {
    struct micros_vm_exit_release_request candidate;
    uint32_t command;

    if (arguments == NULL || request == NULL ||
        !pointer_is_aligned(arguments, _Alignof(struct micros_syscall_arguments)) ||
        !pointer_is_aligned(request, _Alignof(struct micros_vm_exit_release_request)) ||
        micros_vm_handoff_decode_command(arguments, &command) != MICROS_SYSCALL_ABI_OK ||
        command != MICROS_VM_EXIT_RELEASE || arguments->a1 == 0 || arguments->a2 == 0 ||
        arguments->a3 == 0 || arguments->a4 != 0 || arguments->a5 != 0 || arguments->a6 != 0) {
        return MICROS_SYSCALL_ABI_ARGUMENT;
    }
    clear_bytes(&candidate, sizeof(candidate));
    candidate.exit_token = arguments->a1;
    candidate.load_token = arguments->a2;
    candidate.mapping_generation = arguments->a3;
    copy_bytes(request, &candidate, sizeof(candidate));
    return MICROS_SYSCALL_ABI_OK;
}

bool micros_vm_tty_mapping_matches(const struct micros_vm_tty_mapping_request *request,
                                   micros_endpoint_t tty_endpoint) {
    return (request != NULL &&
            pointer_is_aligned(request, _Alignof(struct micros_vm_tty_mapping_request)) &&
            tty_endpoint != MICROS_ENDPOINT_NONE && tty_endpoint != MICROS_ENDPOINT_ANY &&
            request->command == MICROS_VM_HANDOFF_MAP_TTY_UART &&
            request->version == MICROS_TTY_MAPPING_VERSION &&
            request->service_id == MICROS_TTY_SERVICE_ID && request->endpoint == tty_endpoint &&
            request->virtual_base == MICROS_TTY_UART_VIRTUAL_BASE &&
            request->physical_base == MICROS_TTY_UART_PHYSICAL_BASE &&
            request->irq_source == MICROS_TTY_UART_IRQ_SOURCE &&
            request->mapped_length == MICROS_TTY_UART_MAPPED_LENGTH);
}

static bool mapping_record_is_zero(const struct micros_vm_kernel_mapping_record *record) {
    return bytes_are_zero(record, sizeof(*record));
}

static int permission_group(uint32_t permissions) {
    const uint32_t text =
        MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE;
    const uint32_t read_only = MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ;
    const uint32_t writable =
        MICROS_SV39_PERMISSION_USER | MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE;

    if (permissions == text) {
        return 0;
    }
    if (permissions == read_only) {
        return 1;
    }
    return permissions == writable ? 2 : -1;
}

static bool record_entries_are_loading_shape(const struct micros_vm_kernel_mapping_record *record) {
    int group = 0;
    size_t index;

    if (record->entry_count > MICROS_APPLICATION_RESIDENT_PAGE_MAX) {
        return false;
    }
    for (index = 0; index < record->entry_count; ++index) {
        const struct micros_vm_kernel_mapping_entry *entry = &record->entries[index];
        int next_group;
        size_t prior_index;

        if (!mapping_entry_is_valid(entry)) {
            return false;
        }
        for (prior_index = 0; prior_index < index; ++prior_index) {
            if (record->entries[prior_index].physical_address == entry->physical_address) {
                return false;
            }
        }
        if (entry->virtual_address == MICROS_APPLICATION_STACK_BASE) {
            if (index + 1 != record->entry_count || index < MICROS_APPLICATION_IMAGE_PAGE_MIN ||
                index > MICROS_APPLICATION_IMAGE_PAGE_MAX ||
                permission_group(entry->permissions) != 2 || group != 2) {
                return false;
            }
            continue;
        }
        if (index >= MICROS_APPLICATION_IMAGE_PAGE_MAX ||
            entry->virtual_address != MICROS_APPLICATION_ENTRY + index * MICROS_FRAME_SIZE) {
            return false;
        }
        next_group = permission_group(entry->permissions);
        if (next_group < 0 || (index == 0 && next_group != 0) ||
            (index != 0 && (next_group < group || next_group > group + 1))) {
            return false;
        }
        group = next_group;
    }
    for (index = record->entry_count; index < MICROS_APPLICATION_RESIDENT_PAGE_MAX; ++index) {
        if (!bytes_are_zero(&record->entries[index], sizeof(record->entries[index]))) {
            return false;
        }
    }
    return true;
}

static bool loading_record_can_append(const struct micros_vm_kernel_mapping_record *record,
                                      const struct micros_vm_kernel_mapping_entry *entry) {
    int next_group;
    int prior_group = 0;
    size_t index;

    if (!record_entries_are_loading_shape(record) || !mapping_entry_is_valid(entry) ||
        record->entry_count >= MICROS_APPLICATION_RESIDENT_PAGE_MAX) {
        return false;
    }
    for (index = 0; index < record->entry_count; ++index) {
        if (record->entries[index].physical_address == entry->physical_address) {
            return false;
        }
    }
    if (record->entry_count != 0 &&
        record->entries[record->entry_count - 1].virtual_address == MICROS_APPLICATION_STACK_BASE) {
        return false;
    }
    next_group = permission_group(entry->permissions);
    if (entry->virtual_address == MICROS_APPLICATION_STACK_BASE) {
        return (record->entry_count >= MICROS_APPLICATION_IMAGE_PAGE_MIN &&
                record->entry_count <= MICROS_APPLICATION_IMAGE_PAGE_MAX && next_group == 2 &&
                permission_group(record->entries[record->entry_count - 1].permissions) == 2);
    }
    if (record->entry_count >= MICROS_APPLICATION_IMAGE_PAGE_MAX ||
        entry->virtual_address !=
            MICROS_APPLICATION_ENTRY + record->entry_count * MICROS_FRAME_SIZE ||
        next_group < 0) {
        return false;
    }
    if (record->entry_count == 0) {
        return next_group == 0;
    }
    prior_group = permission_group(record->entries[record->entry_count - 1].permissions);
    return (next_group >= prior_group && next_group <= prior_group + 1);
}

static bool record_entries_are_frozen_shape(const struct micros_vm_kernel_mapping_record *record) {
    bool saw_read_only = false;
    bool saw_writable = false;
    size_t image_page_count;
    size_t index;

    if (record->entry_count < MICROS_APPLICATION_IMAGE_PAGE_MIN + 1 ||
        record->entry_count > MICROS_APPLICATION_RESIDENT_PAGE_MAX ||
        !record_entries_are_loading_shape(record)) {
        return false;
    }
    image_page_count = record->entry_count - 1;
    if (image_page_count < MICROS_APPLICATION_IMAGE_PAGE_MIN ||
        image_page_count > MICROS_APPLICATION_IMAGE_PAGE_MAX ||
        record->entries[image_page_count].virtual_address != MICROS_APPLICATION_STACK_BASE ||
        permission_group(record->entries[image_page_count - 1].permissions) != 2) {
        return false;
    }
    for (index = 0; index < image_page_count; ++index) {
        int group = permission_group(record->entries[index].permissions);

        saw_read_only = saw_read_only || group == 1;
        saw_writable = saw_writable || group == 2;
    }
    return saw_read_only && saw_writable;
}

static bool frozen_record_is_valid(const struct micros_vm_kernel_mapping_record *record) {
    size_t table_index;
    size_t entry_index;

    if (record->mapping_generation == 0 ||
        !address_is_page_aligned(record->root_physical_address) ||
        record->root_physical_address != record->table_frames[0] ||
        record->entry != MICROS_APPLICATION_ENTRY ||
        !stack_pointer_is_valid(record->stack_pointer) ||
        !record_entries_are_frozen_shape(record)) {
        return false;
    }
    for (table_index = 0; table_index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++table_index) {
        if (!address_is_page_aligned(record->table_frames[table_index])) {
            return false;
        }
        for (entry_index = 0; entry_index < table_index; ++entry_index) {
            if (record->table_frames[entry_index] == record->table_frames[table_index]) {
                return false;
            }
        }
        for (entry_index = 0; entry_index < record->entry_count; ++entry_index) {
            if (record->entries[entry_index].physical_address ==
                record->table_frames[table_index]) {
                return false;
            }
        }
    }
    return true;
}

static bool record_contains_physical_address(const struct micros_vm_kernel_mapping_record *record,
                                             uint64_t physical_address) {
    size_t index;

    for (index = 0; index < record->entry_count; ++index) {
        if (record->entries[index].physical_address == physical_address) {
            return true;
        }
    }
    if (record->phase == MICROS_VM_KERNEL_MAPPING_LOADING) {
        return false;
    }
    for (index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
        if (record->table_frames[index] == physical_address) {
            return true;
        }
    }
    return false;
}

static bool record_overlaps_prior_records(const struct micros_vm_handoff_state *state,
                                          size_t slot) {
    const struct micros_vm_kernel_mapping_record *record = &state->mappings[slot];
    size_t prior_slot;
    size_t index;

    for (prior_slot = 0; prior_slot < slot; ++prior_slot) {
        const struct micros_vm_kernel_mapping_record *prior = &state->mappings[prior_slot];

        if (prior->phase == MICROS_VM_KERNEL_MAPPING_UNUSED) {
            continue;
        }
        for (index = 0; index < record->entry_count; ++index) {
            if (record_contains_physical_address(prior, record->entries[index].physical_address)) {
                return true;
            }
        }
        if (record->phase != MICROS_VM_KERNEL_MAPPING_LOADING) {
            for (index = 0; index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++index) {
                if (record_contains_physical_address(prior, record->table_frames[index])) {
                    return true;
                }
            }
        }
    }
    return false;
}

static bool record_tokens_are_unique(const struct micros_vm_handoff_state *state, size_t slot) {
    const struct micros_vm_kernel_mapping_record *record = &state->mappings[slot];
    size_t prior_slot;

    for (prior_slot = 0; prior_slot < slot; ++prior_slot) {
        const struct micros_vm_kernel_mapping_record *prior = &state->mappings[prior_slot];

        if (prior->phase == MICROS_VM_KERNEL_MAPPING_UNUSED) {
            continue;
        }
        if (prior->load_token == record->load_token ||
            (record->mapping_generation != 0 &&
             prior->mapping_generation == record->mapping_generation) ||
            (record->exit_token != 0 && prior->exit_token == record->exit_token)) {
            return false;
        }
    }
    return true;
}

static bool mapping_record_is_valid(const struct micros_vm_handoff_state *state, size_t slot) {
    const struct micros_vm_kernel_mapping_record *record = &state->mappings[slot];

    if (record->phase == MICROS_VM_KERNEL_MAPPING_UNUSED) {
        return mapping_record_is_zero(record);
    }
    if (record->entry_count > MICROS_APPLICATION_RESIDENT_PAGE_MAX ||
        !process_is_valid(record->process) || record->process.slot != slot ||
        process_handles_equal(record->process, state->process) || record->load_token == 0 ||
        record->load_token > state->last_load_token || !record_tokens_are_unique(state, slot) ||
        record_overlaps_prior_records(state, slot)) {
        return false;
    }
    if (record->phase == MICROS_VM_KERNEL_MAPPING_LOADING) {
        return (record->mapping_generation == 0 && record->exit_token == 0 &&
                record->root_physical_address == 0 &&
                bytes_are_zero(record->table_frames, sizeof(record->table_frames)) &&
                record->entry == 0 && record->stack_pointer == 0 &&
                record_entries_are_loading_shape(record));
    }
    if (record->phase != MICROS_VM_KERNEL_MAPPING_FROZEN &&
        record->phase != MICROS_VM_KERNEL_MAPPING_ACTIVATED_PENDING_COMMIT &&
        record->phase != MICROS_VM_KERNEL_MAPPING_ACTIVE &&
        record->phase != MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED) {
        return false;
    }
    return (record->mapping_generation <= state->last_mapping_generation &&
            frozen_record_is_valid(record) &&
            ((record->phase == MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED && record->exit_token != 0) ||
             (record->phase != MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED && record->exit_token == 0)));
}

enum micros_vm_handoff_error micros_vm_handoff_state_prepare(
    struct micros_vm_handoff_state *state, uint32_t service_id, uint32_t endpoint,
    uint32_t image_id, uint32_t profile_id, struct micros_process_handle process,
    struct micros_thread_handle thread, uint64_t root_physical_address, uint64_t boot_info_address,
    uint32_t boot_info_size, const struct micros_vm_boot_summary *summary) {
    if (state == NULL || summary == NULL ||
        !pointer_is_aligned(state, _Alignof(struct micros_vm_handoff_state)) ||
        !pointer_is_aligned(summary, _Alignof(struct micros_vm_boot_summary))) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    if (!bytes_are_zero(state, sizeof(*state))) {
        return MICROS_VM_HANDOFF_ERROR_STORAGE;
    }
    if (service_id == 0 || service_id > 63 || !endpoint_matches_process(endpoint, process) ||
        image_id == 0 || profile_id == 0 || profile_id >= MICROS_PRIVILEGE_PROFILE_CAPACITY ||
        !thread_is_valid(thread) || root_physical_address == 0 ||
        root_physical_address % MICROS_FRAME_SIZE != 0 || boot_info_address == 0 ||
        boot_info_address % MICROS_VM_BOOT_INFO_ALIGNMENT != 0 ||
        boot_info_size != MICROS_VM_BOOT_INFO_SIZE || !summary_is_valid(summary)) {
        return MICROS_VM_HANDOFF_ERROR_IDENTITY;
    }
    state->initialization_magic = MICROS_VM_HANDOFF_MAGIC;
    state->phase = MICROS_VM_HANDOFF_PHASE_PREPARED;
    state->service_id = service_id;
    state->endpoint = endpoint;
    state->image_id = image_id;
    state->profile_id = profile_id;
    state->process.slot = process.slot;
    state->process.generation = process.generation;
    state->thread.slot = thread.slot;
    state->thread.generation = thread.generation;
    state->root_physical_address = root_physical_address;
    state->boot_info_address = boot_info_address;
    state->boot_info_size = boot_info_size;
    copy_bytes(&state->summary, summary, sizeof(state->summary));
    return MICROS_VM_HANDOFF_OK;
}

enum micros_vm_handoff_error
micros_vm_handoff_state_validate(const struct micros_vm_handoff_state *state) {
    size_t loading_slot = MICROS_PROCESS_CAPACITY;
    size_t frozen_slot = MICROS_PROCESS_CAPACITY;
    size_t slot;

    if (state == NULL || !pointer_is_aligned(state, _Alignof(struct micros_vm_handoff_state))) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    if (state->initialization_magic != MICROS_VM_HANDOFF_MAGIC ||
        (state->phase != MICROS_VM_HANDOFF_PHASE_PREPARED &&
         state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF) ||
        state->service_id == 0 || state->service_id > 63 ||
        !endpoint_matches_process(state->endpoint, state->process) || state->image_id == 0 ||
        state->profile_id == 0 || state->profile_id >= MICROS_PRIVILEGE_PROFILE_CAPACITY ||
        !thread_is_valid(state->thread) || state->root_physical_address == 0 ||
        state->root_physical_address % MICROS_FRAME_SIZE != 0 || state->boot_info_address == 0 ||
        state->boot_info_address % MICROS_VM_BOOT_INFO_ALIGNMENT != 0 ||
        state->boot_info_size != MICROS_VM_BOOT_INFO_SIZE || state->reserved != 0 ||
        !summary_is_valid(&state->summary) ||
        state->last_mapping_generation > state->last_load_token) {
        return MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    if (state->phase == MICROS_VM_HANDOFF_PHASE_PREPARED) {
        if (state->last_load_token != 0 || state->last_mapping_generation != 0 ||
            !bytes_are_zero(&state->load, sizeof(state->load))) {
            return MICROS_VM_HANDOFF_ERROR_INVARIANT;
        }
        for (slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
            if (!mapping_record_is_zero(&state->mappings[slot])) {
                return MICROS_VM_HANDOFF_ERROR_INVARIANT;
            }
        }
        return MICROS_VM_HANDOFF_OK;
    }

    for (slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
        const struct micros_vm_kernel_mapping_record *record = &state->mappings[slot];

        if (!mapping_record_is_valid(state, slot)) {
            return MICROS_VM_HANDOFF_ERROR_INVARIANT;
        }
        if (record->phase == MICROS_VM_KERNEL_MAPPING_LOADING) {
            if (loading_slot != MICROS_PROCESS_CAPACITY) {
                return MICROS_VM_HANDOFF_ERROR_INVARIANT;
            }
            loading_slot = slot;
        } else if (record->phase == MICROS_VM_KERNEL_MAPPING_FROZEN ||
                   record->phase == MICROS_VM_KERNEL_MAPPING_ACTIVATED_PENDING_COMMIT) {
            if (frozen_slot != MICROS_PROCESS_CAPACITY) {
                return MICROS_VM_HANDOFF_ERROR_INVARIANT;
            }
            frozen_slot = slot;
        }
    }

    if (state->load.phase == MICROS_VM_LOAD_PHASE_IDLE) {
        if (!bytes_are_zero(&state->load, sizeof(state->load)) ||
            loading_slot != MICROS_PROCESS_CAPACITY || frozen_slot != MICROS_PROCESS_CAPACITY) {
            return MICROS_VM_HANDOFF_ERROR_INVARIANT;
        }
        return MICROS_VM_HANDOFF_OK;
    }
    if (state->load.phase != MICROS_VM_LOAD_PHASE_LOADING &&
        state->load.phase != MICROS_VM_LOAD_PHASE_FROZEN) {
        return MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    if (state->load.process_reservation_token == 0 || state->load.load_token == 0 ||
        state->load.load_token > state->last_load_token || !process_is_valid(state->load.process) ||
        process_handles_equal(state->load.process, state->process) || state->load.reserved != 0 ||
        state->load.scratch_phase > MICROS_VM_SCRATCH_MAPPED) {
        return MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    if (state->load.phase == MICROS_VM_LOAD_PHASE_LOADING) {
        const struct micros_vm_kernel_mapping_record *record;

        if (loading_slot != state->load.process.slot || frozen_slot != MICROS_PROCESS_CAPACITY) {
            return MICROS_VM_HANDOFF_ERROR_INVARIANT;
        }
        record = &state->mappings[loading_slot];
        if (!process_handles_equal(record->process, state->load.process) ||
            record->load_token != state->load.load_token) {
            return MICROS_VM_HANDOFF_ERROR_INVARIANT;
        }
        if (state->load.scratch_phase == MICROS_VM_SCRATCH_CLEAR) {
            if (!bytes_are_zero(&state->load.scratch, sizeof(state->load.scratch))) {
                return MICROS_VM_HANDOFF_ERROR_INVARIANT;
            }
        } else {
            if (!loading_record_can_append(record, &state->load.scratch)) {
                return MICROS_VM_HANDOFF_ERROR_INVARIANT;
            }
            for (slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
                if (record_contains_physical_address(&state->mappings[slot],
                                                     state->load.scratch.physical_address)) {
                    return MICROS_VM_HANDOFF_ERROR_INVARIANT;
                }
            }
        }
        return MICROS_VM_HANDOFF_OK;
    }

    if (state->load.scratch_phase != MICROS_VM_SCRATCH_CLEAR ||
        !bytes_are_zero(&state->load.scratch, sizeof(state->load.scratch)) ||
        loading_slot != MICROS_PROCESS_CAPACITY || frozen_slot != state->load.process.slot ||
        !process_handles_equal(state->mappings[frozen_slot].process, state->load.process) ||
        state->mappings[frozen_slot].load_token != state->load.load_token) {
        return MICROS_VM_HANDOFF_ERROR_INVARIANT;
    }
    return MICROS_VM_HANDOFF_OK;
}

bool micros_vm_handoff_summary_matches(const struct micros_vm_handoff_state *state,
                                       const struct micros_vm_handoff_request *request) {
    return (state != NULL && request != NULL &&
            pointer_is_aligned(state, _Alignof(struct micros_vm_handoff_state)) &&
            pointer_is_aligned(request, _Alignof(struct micros_vm_handoff_request)) &&
            micros_vm_handoff_state_validate(state) == MICROS_VM_HANDOFF_OK &&
            request->command == MICROS_VM_HANDOFF_READY &&
            request->version == state->summary.version &&
            request->managed_range_count == state->summary.managed_range_count &&
            request->address_space_count == state->summary.address_space_count &&
            request->managed_frame_count == state->summary.managed_frame_count &&
            request->free_frame_count == state->summary.free_frame_count &&
            request->mapping_count == state->summary.mapping_count &&
            request->digest == state->summary.digest);
}

void micros_vm_handoff_commit_prevalidated(struct micros_vm_handoff_state *state) {
    state->phase = MICROS_VM_HANDOFF_PHASE_HANDED_OFF;
}

static uint64_t page_digest_byte(uint64_t digest, uint8_t byte) {
    digest ^= byte;
    return digest * MICROS_VM_PAGE_DIGEST_PRIME;
}

static uint64_t page_digest_u32(uint64_t digest, uint32_t value) {
    size_t index;

    for (index = 0; index < 4; ++index) {
        digest = page_digest_byte(digest, (uint8_t)(value >> (index * 8)));
    }
    return digest;
}

static uint64_t page_digest_u64(uint64_t digest, uint64_t value) {
    size_t index;

    for (index = 0; index < 8; ++index) {
        digest = page_digest_byte(digest, (uint8_t)(value >> (index * 8)));
    }
    return digest;
}

uint64_t micros_vm_load_page_plan_digest(uint32_t image_page_count, uint32_t data_page_count,
                                         const struct micros_vm_kernel_mapping_entry *entries) {
    uint64_t digest = MICROS_VM_PAGE_DIGEST_OFFSET;
    size_t index;

    if (entries == NULL ||
        !pointer_is_aligned(entries, _Alignof(struct micros_vm_kernel_mapping_entry)) ||
        image_page_count < MICROS_APPLICATION_IMAGE_PAGE_MIN ||
        image_page_count > MICROS_APPLICATION_IMAGE_PAGE_MAX ||
        data_page_count != image_page_count + 1 ||
        data_page_count > MICROS_APPLICATION_RESIDENT_PAGE_MAX) {
        return 0;
    }
    digest = page_digest_u32(digest, image_page_count);
    digest = page_digest_u32(digest, data_page_count);
    for (index = 0; index < data_page_count; ++index) {
        digest = page_digest_u64(digest, entries[index].virtual_address);
        digest = page_digest_u64(digest, entries[index].physical_address);
        digest = page_digest_u32(digest, entries[index].permissions);
        digest = page_digest_u32(digest, 0);
    }
    return digest;
}

enum micros_vm_handoff_error
micros_vm_load_begin_preflight(const struct micros_vm_handoff_state *state,
                               uint64_t process_reservation_token,
                               struct micros_process_handle process, bool process_is_empty,
                               struct micros_vm_load_begin_plan *plan) {
    struct micros_vm_load_begin_plan candidate;
    enum micros_vm_handoff_error error;
    uint64_t load_token;

    if (plan == NULL || !pointer_is_aligned(plan, _Alignof(struct micros_vm_load_begin_plan))) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_vm_handoff_state_validate(state);
    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF ||
        state->load.phase != MICROS_VM_LOAD_PHASE_IDLE || process_reservation_token == 0 ||
        !process_is_valid(process) || process_handles_equal(process, state->process) ||
        !process_is_empty || !mapping_record_is_zero(&state->mappings[process.slot])) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    load_token = next_nonzero_token(state->last_load_token);
    if (load_token == 0) {
        return MICROS_VM_HANDOFF_ERROR_CAPACITY;
    }

    clear_bytes(&candidate, sizeof(candidate));
    candidate.process_reservation_token = process_reservation_token;
    candidate.load_token = load_token;
    candidate.process = process;
    candidate.result.version = MICROS_VM_LOAD_BEGIN_RESULT_VERSION;
    candidate.result.size = MICROS_VM_LOAD_BEGIN_RESULT_SIZE;
    candidate.result.load_token = load_token;
    candidate.result.process_slot = process.slot;
    candidate.result.process_generation = process.generation;
    copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_VM_HANDOFF_OK;
}

void micros_vm_load_begin_commit_prevalidated(struct micros_vm_handoff_state *state,
                                              const struct micros_vm_load_begin_plan *plan) {
    struct micros_vm_kernel_mapping_record *record = &state->mappings[plan->process.slot];

    state->last_load_token = plan->load_token;
    state->load.phase = MICROS_VM_LOAD_PHASE_LOADING;
    state->load.scratch_phase = MICROS_VM_SCRATCH_CLEAR;
    state->load.process_reservation_token = plan->process_reservation_token;
    state->load.load_token = plan->load_token;
    state->load.process = plan->process;

    record->load_token = plan->load_token;
    record->process = plan->process;
    record->phase = MICROS_VM_KERNEL_MAPPING_LOADING;
}

enum micros_vm_handoff_error
micros_vm_scratch_map_preflight(const struct micros_vm_handoff_state *state,
                                const struct micros_vm_scratch_map_request *request,
                                struct micros_vm_scratch_plan *plan) {
    struct micros_vm_scratch_plan candidate;
    struct micros_vm_kernel_mapping_entry entry;
    const struct micros_vm_kernel_mapping_record *record;
    enum micros_vm_handoff_error error;
    size_t slot;

    if (request == NULL || plan == NULL ||
        !pointer_is_aligned(request, _Alignof(struct micros_vm_scratch_map_request)) ||
        !pointer_is_aligned(plan, _Alignof(struct micros_vm_scratch_plan))) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_vm_handoff_state_validate(state);
    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF ||
        state->load.phase != MICROS_VM_LOAD_PHASE_LOADING ||
        state->load.scratch_phase != MICROS_VM_SCRATCH_CLEAR ||
        request->load_token != state->load.load_token) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }

    clear_bytes(&entry, sizeof(entry));
    entry.virtual_address = request->virtual_address;
    entry.physical_address = request->physical_address;
    entry.permissions = request->permissions;
    record = &state->mappings[state->load.process.slot];
    if (!mapping_entry_is_valid(&entry)) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    if (!loading_record_can_append(record, &entry)) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    for (slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
        if (record_contains_physical_address(&state->mappings[slot], entry.physical_address)) {
            return MICROS_VM_HANDOFF_ERROR_STATE;
        }
    }

    clear_bytes(&candidate, sizeof(candidate));
    candidate.entry = entry;
    copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_VM_HANDOFF_OK;
}

void micros_vm_scratch_map_commit_prevalidated(struct micros_vm_handoff_state *state,
                                               const struct micros_vm_scratch_plan *plan) {
    state->load.scratch = plan->entry;
    state->load.scratch_phase = MICROS_VM_SCRATCH_MAPPED;
}

enum micros_vm_handoff_error
micros_vm_scratch_commit_preflight(const struct micros_vm_handoff_state *state,
                                   const struct micros_vm_scratch_commit_request *request,
                                   struct micros_vm_scratch_plan *plan) {
    struct micros_vm_scratch_plan candidate;
    enum micros_vm_handoff_error error;

    if (request == NULL || plan == NULL ||
        !pointer_is_aligned(request, _Alignof(struct micros_vm_scratch_commit_request)) ||
        !pointer_is_aligned(plan, _Alignof(struct micros_vm_scratch_plan))) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_vm_handoff_state_validate(state);
    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF ||
        state->load.phase != MICROS_VM_LOAD_PHASE_LOADING ||
        state->load.scratch_phase != MICROS_VM_SCRATCH_MAPPED ||
        request->load_token != state->load.load_token ||
        request->virtual_address != state->load.scratch.virtual_address ||
        request->physical_address != state->load.scratch.physical_address) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    clear_bytes(&candidate, sizeof(candidate));
    candidate.entry = state->load.scratch;
    copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_VM_HANDOFF_OK;
}

void micros_vm_scratch_commit_commit_prevalidated(struct micros_vm_handoff_state *state,
                                                  const struct micros_vm_scratch_plan *plan) {
    struct micros_vm_kernel_mapping_record *record = &state->mappings[state->load.process.slot];

    record->entries[record->entry_count] = plan->entry;
    ++record->entry_count;
    clear_bytes(&state->load.scratch, sizeof(state->load.scratch));
    state->load.scratch_phase = MICROS_VM_SCRATCH_CLEAR;
}

static bool freeze_table_frames_are_valid(const struct micros_vm_handoff_state *state,
                                          const struct micros_vm_kernel_mapping_record *record,
                                          const struct micros_vm_load_freeze_plan *request) {
    size_t slot;
    size_t table_index;
    size_t prior_index;

    for (table_index = 0; table_index < MICROS_VM_LOAD_PAGE_TABLE_FRAME_COUNT; ++table_index) {
        if (!address_is_page_aligned(request->table_frames[table_index])) {
            return false;
        }
        for (prior_index = 0; prior_index < table_index; ++prior_index) {
            if (request->table_frames[prior_index] == request->table_frames[table_index]) {
                return false;
            }
        }
        for (prior_index = 0; prior_index < record->entry_count; ++prior_index) {
            if (record->entries[prior_index].physical_address ==
                request->table_frames[table_index]) {
                return false;
            }
        }
        for (slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
            if (&state->mappings[slot] != record &&
                record_contains_physical_address(&state->mappings[slot],
                                                 request->table_frames[table_index])) {
                return false;
            }
        }
    }
    return true;
}

enum micros_vm_handoff_error
micros_vm_load_freeze_preflight(const struct micros_vm_handoff_state *state,
                                const struct micros_vm_load_freeze_plan *request,
                                struct micros_vm_load_freeze_state_plan *plan) {
    struct micros_vm_load_freeze_state_plan candidate;
    const struct micros_vm_kernel_mapping_record *record;
    enum micros_vm_handoff_error error;
    uint64_t mapping_generation;

    if (request == NULL || plan == NULL ||
        !pointer_is_aligned(request, _Alignof(struct micros_vm_load_freeze_plan)) ||
        !pointer_is_aligned(plan, _Alignof(struct micros_vm_load_freeze_state_plan))) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_vm_handoff_state_validate(state);
    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF ||
        state->load.phase != MICROS_VM_LOAD_PHASE_LOADING ||
        state->load.scratch_phase != MICROS_VM_SCRATCH_CLEAR ||
        request->load_token != state->load.load_token) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    record = &state->mappings[state->load.process.slot];
    if (request->version != MICROS_VM_LOAD_FREEZE_PLAN_VERSION ||
        request->size != MICROS_VM_LOAD_FREEZE_PLAN_SIZE || request->reserved[0] != 0 ||
        request->reserved[1] != 0 || request->entry != MICROS_APPLICATION_ENTRY ||
        !stack_pointer_is_valid(request->stack_pointer) ||
        request->image_page_count < MICROS_APPLICATION_IMAGE_PAGE_MIN ||
        request->image_page_count > MICROS_APPLICATION_IMAGE_PAGE_MAX ||
        request->data_page_count != request->image_page_count + 1 ||
        request->data_page_count != record->entry_count ||
        !record_entries_are_frozen_shape(record) ||
        !freeze_table_frames_are_valid(state, record, request)) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    if (request->page_plan_digest != micros_vm_load_page_plan_digest(request->image_page_count,
                                                                     request->data_page_count,
                                                                     record->entries)) {
        return MICROS_VM_HANDOFF_ERROR_SUMMARY;
    }
    mapping_generation = next_nonzero_token(state->last_mapping_generation);
    if (mapping_generation == 0) {
        return MICROS_VM_HANDOFF_ERROR_CAPACITY;
    }

    clear_bytes(&candidate, sizeof(candidate));
    candidate.mapping_generation = mapping_generation;
    candidate.action.process = state->load.process;
    candidate.action.load_token = state->load.load_token;
    candidate.action.mapping_generation = mapping_generation;
    candidate.action.root_physical_address = request->table_frames[0];
    copy_bytes(candidate.action.table_frames, request->table_frames,
               sizeof(candidate.action.table_frames));
    candidate.action.entry = request->entry;
    candidate.action.stack_pointer = request->stack_pointer;
    candidate.action.entry_count = record->entry_count;
    copy_bytes(candidate.action.entries, record->entries, sizeof(candidate.action.entries));
    candidate.result.version = MICROS_VM_LOAD_FREEZE_RESULT_VERSION;
    candidate.result.size = MICROS_VM_LOAD_FREEZE_RESULT_SIZE;
    candidate.result.mapping_generation = mapping_generation;
    copy_bytes(plan, &candidate, sizeof(candidate));
    return MICROS_VM_HANDOFF_OK;
}

void micros_vm_load_freeze_commit_prevalidated(
    struct micros_vm_handoff_state *state, const struct micros_vm_load_freeze_state_plan *plan) {
    struct micros_vm_kernel_mapping_record *record = &state->mappings[state->load.process.slot];

    state->last_mapping_generation = plan->mapping_generation;
    state->load.phase = MICROS_VM_LOAD_PHASE_FROZEN;
    record->mapping_generation = plan->mapping_generation;
    record->root_physical_address = plan->action.root_physical_address;
    copy_bytes(record->table_frames, plan->action.table_frames, sizeof(record->table_frames));
    record->entry = plan->action.entry;
    record->stack_pointer = plan->action.stack_pointer;
    record->phase = MICROS_VM_KERNEL_MAPPING_FROZEN;
}

enum micros_vm_handoff_error
micros_vm_load_abort_preflight(const struct micros_vm_handoff_state *state, uint64_t load_token,
                               uint64_t mapping_generation,
                               struct micros_vm_load_abort_action *action) {
    struct micros_vm_load_abort_action candidate;
    const struct micros_vm_kernel_mapping_record *record;
    enum micros_vm_handoff_error error;

    if (action == NULL ||
        !pointer_is_aligned(action, _Alignof(struct micros_vm_load_abort_action))) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_vm_handoff_state_validate(state);
    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF ||
        state->load.phase == MICROS_VM_LOAD_PHASE_IDLE || load_token == 0 ||
        load_token != state->load.load_token) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    record = &state->mappings[state->load.process.slot];
    if ((state->load.phase == MICROS_VM_LOAD_PHASE_LOADING && mapping_generation != 0) ||
        (state->load.phase == MICROS_VM_LOAD_PHASE_FROZEN &&
         (mapping_generation != record->mapping_generation ||
          record->phase != MICROS_VM_KERNEL_MAPPING_FROZEN))) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }

    clear_bytes(&candidate, sizeof(candidate));
    candidate.frozen = state->load.phase == MICROS_VM_LOAD_PHASE_FROZEN;
    candidate.scratch_mapped = state->load.scratch_phase == MICROS_VM_SCRATCH_MAPPED;
    candidate.process = state->load.process;
    candidate.root_physical_address = record->root_physical_address;
    candidate.scratch_physical_address =
        candidate.scratch_mapped ? state->load.scratch.physical_address : 0;
    candidate.entry_count = record->entry_count;
    copy_bytes(candidate.table_frames, record->table_frames, sizeof(candidate.table_frames));
    copy_bytes(candidate.entries, record->entries, sizeof(candidate.entries));
    copy_bytes(action, &candidate, sizeof(candidate));
    return MICROS_VM_HANDOFF_OK;
}

void micros_vm_load_abort_commit_prevalidated(struct micros_vm_handoff_state *state) {
    clear_bytes(&state->mappings[state->load.process.slot],
                sizeof(state->mappings[state->load.process.slot]));
    clear_bytes(&state->load, sizeof(state->load));
}

enum micros_vm_handoff_error micros_vm_load_mark_activated(struct micros_vm_handoff_state *state,
                                                           struct micros_process_handle process,
                                                           uint64_t process_reservation_token,
                                                           uint64_t load_token,
                                                           uint64_t mapping_generation) {
    struct micros_vm_kernel_mapping_record *record;
    enum micros_vm_handoff_error error = micros_vm_handoff_state_validate(state);

    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF ||
        state->load.phase != MICROS_VM_LOAD_PHASE_FROZEN ||
        !process_handles_equal(process, state->load.process) ||
        process_reservation_token != state->load.process_reservation_token ||
        load_token != state->load.load_token) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    record = &state->mappings[process.slot];
    if (record->phase != MICROS_VM_KERNEL_MAPPING_FROZEN ||
        record->mapping_generation != mapping_generation) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    record->phase = MICROS_VM_KERNEL_MAPPING_ACTIVATED_PENDING_COMMIT;
    return MICROS_VM_HANDOFF_OK;
}

enum micros_vm_handoff_error
micros_vm_load_commit_preflight(const struct micros_vm_handoff_state *state, uint64_t load_token,
                                uint64_t mapping_generation) {
    const struct micros_vm_kernel_mapping_record *record;
    enum micros_vm_handoff_error error = micros_vm_handoff_state_validate(state);

    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF ||
        state->load.phase != MICROS_VM_LOAD_PHASE_FROZEN || load_token != state->load.load_token) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    record = &state->mappings[state->load.process.slot];
    return (record->phase == MICROS_VM_KERNEL_MAPPING_ACTIVATED_PENDING_COMMIT &&
            record->mapping_generation == mapping_generation)
               ? MICROS_VM_HANDOFF_OK
               : MICROS_VM_HANDOFF_ERROR_STATE;
}

void micros_vm_load_commit_commit_prevalidated(struct micros_vm_handoff_state *state) {
    state->mappings[state->load.process.slot].phase = MICROS_VM_KERNEL_MAPPING_ACTIVE;
    clear_bytes(&state->load, sizeof(state->load));
}

enum micros_vm_handoff_error
micros_vm_load_mark_exit_detached(struct micros_vm_handoff_state *state,
                                  struct micros_process_handle process, uint64_t exit_token,
                                  uint64_t load_token, uint64_t mapping_generation) {
    struct micros_vm_kernel_mapping_record *record;
    enum micros_vm_handoff_error error = micros_vm_handoff_state_validate(state);

    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF || !process_is_valid(process) ||
        exit_token == 0) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    record = &state->mappings[process.slot];
    if (record->phase != MICROS_VM_KERNEL_MAPPING_ACTIVE ||
        !process_handles_equal(record->process, process) || record->load_token != load_token ||
        record->mapping_generation != mapping_generation) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }
    record->exit_token = exit_token;
    record->phase = MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED;
    return MICROS_VM_HANDOFF_OK;
}

enum micros_vm_handoff_error
micros_vm_exit_release_preflight(const struct micros_vm_handoff_state *state, uint64_t exit_token,
                                 uint64_t load_token, uint64_t mapping_generation,
                                 struct micros_vm_exit_release_action *action) {
    struct micros_vm_exit_release_action candidate;
    const struct micros_vm_kernel_mapping_record *record = NULL;
    enum micros_vm_handoff_error error;
    size_t slot;

    if (action == NULL ||
        !pointer_is_aligned(action, _Alignof(struct micros_vm_exit_release_action))) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_vm_handoff_state_validate(state);
    if (error != MICROS_VM_HANDOFF_OK) {
        return error;
    }
    if (state->phase != MICROS_VM_HANDOFF_PHASE_HANDED_OFF || exit_token == 0 || load_token == 0 ||
        mapping_generation == 0) {
        return MICROS_VM_HANDOFF_ERROR_ARGUMENT;
    }
    for (slot = 0; slot < MICROS_PROCESS_CAPACITY; ++slot) {
        const struct micros_vm_kernel_mapping_record *candidate_record = &state->mappings[slot];

        if (candidate_record->phase == MICROS_VM_KERNEL_MAPPING_EXIT_DETACHED &&
            candidate_record->exit_token == exit_token) {
            if (record != NULL) {
                return MICROS_VM_HANDOFF_ERROR_INVARIANT;
            }
            record = candidate_record;
        }
    }
    if (record == NULL || record->load_token != load_token ||
        record->mapping_generation != mapping_generation) {
        return MICROS_VM_HANDOFF_ERROR_STATE;
    }

    clear_bytes(&candidate, sizeof(candidate));
    candidate.process = record->process;
    candidate.root_physical_address = record->root_physical_address;
    candidate.entry_count = record->entry_count;
    copy_bytes(candidate.table_frames, record->table_frames, sizeof(candidate.table_frames));
    copy_bytes(candidate.entries, record->entries, sizeof(candidate.entries));
    copy_bytes(action, &candidate, sizeof(candidate));
    return MICROS_VM_HANDOFF_OK;
}

void micros_vm_exit_release_commit_prevalidated(struct micros_vm_handoff_state *state,
                                                struct micros_process_handle process) {
    clear_bytes(&state->mappings[process.slot], sizeof(state->mappings[process.slot]));
}

enum micros_vm_self_fault_action
micros_vm_self_fault_classify(enum micros_bootstrap_phase bootstrap_phase,
                              enum micros_bootstrap_service_state service_state) {
    if (bootstrap_phase == MICROS_BOOTSTRAP_PHASE_RUNNING) {
        if (service_state == MICROS_BOOTSTRAP_SERVICE_STARTING) {
            return MICROS_VM_SELF_FAULT_RUNNING_STARTING;
        }
        if (service_state == MICROS_BOOTSTRAP_SERVICE_READY) {
            return MICROS_VM_SELF_FAULT_RUNNING_READY;
        }
        return MICROS_VM_SELF_FAULT_INVALID;
    }
    return bootstrap_phase == MICROS_BOOTSTRAP_PHASE_SEALED ? MICROS_VM_SELF_FAULT_SEALED
                                                            : MICROS_VM_SELF_FAULT_INVALID;
}
