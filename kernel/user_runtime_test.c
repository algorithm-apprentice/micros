#include "kernel/user_runtime_test.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/riscv64/interrupt.h"
#include "arch/riscv64/mmu.h"
#include "arch/riscv64/platform.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/grant_runtime_internal.h"
#include "kernel/ipc_runtime_internal.h"
#include "kernel/kernel_object_runtime_internal.h"
#include "kernel/user_runtime_image.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/grant_runtime.h"
#include "micros/ipc_core.h"
#include "micros/ipc_runtime.h"
#include "micros/kernel_address_space.h"
#include "micros/kernel_object_runtime.h"
#include "micros/scheduler.h"
#include "micros/scheduler_core.h"
#include "micros/sv39.h"
#include "micros/user_address_space.h"
#include "micros/user_execution.h"
#include "tests/qemu/user_runtime_protocol.h"

#define TEST_SSTATUS_UBE (UINT64_C(1) << 6)
#define TEST_SSTATUS_VS (UINT64_C(3) << 9)
#define TEST_SSTATUS_FS (UINT64_C(3) << 13)
#define TEST_SSTATUS_XS (UINT64_C(3) << 15)
#define TEST_SSTATUS_MXR (UINT64_C(1) << 19)
#define TEST_SSTATUS_SD (UINT64_C(1) << 63)

enum {
    USER_RUNTIME_CLIENT = 0,
    USER_RUNTIME_SERVER,
    USER_RUNTIME_PEER,
    USER_RUNTIME_PROCESS_COUNT,
    USER_RUNTIME_MAX_IMAGE_PAGES = 8,
    USER_RUNTIME_PROGRAM_EXECUTE = 1,
    USER_RUNTIME_PROGRAM_WRITE = 2,
    USER_RUNTIME_PROGRAM_READ = 4,
    USER_RUNTIME_EXCEPTION_BREAKPOINT = 3,
    USER_RUNTIME_EXCEPTION_STORE_PAGE_FAULT = 15,
};

static const uint64_t USER_RUNTIME_TIMER_INTERVAL =
    UINT64_C(0x000000003b9aca00);

struct mapped_page {
    uint64_t virtual_address;
    uint64_t physical_address;
    uint32_t permissions;
    size_t segment_index;
};

struct process_image {
    size_t page_count;
    struct mapped_page pages[USER_RUNTIME_MAX_IMAGE_PAGES];
    uint64_t stack_physical;
};

extern const unsigned char micros_user_runtime_test_supervisor_resume[];

uintptr_t micros_user_runtime_test_saved_sp;
uintptr_t micros_user_runtime_test_saved_gp;
uintptr_t micros_user_runtime_test_saved_tp;

static struct micros_process_handle
    processes[USER_RUNTIME_PROCESS_COUNT];
static struct micros_thread_handle threads[USER_RUNTIME_PROCESS_COUNT];
static micros_endpoint_t endpoints[USER_RUNTIME_PROCESS_COUNT];
static struct process_image images[USER_RUNTIME_PROCESS_COUNT];
static unsigned char writable_snapshots[
    USER_RUNTIME_PROCESS_COUNT
][USER_RUNTIME_MAX_IMAGE_PAGES][MICROS_SV39_PAGE_SIZE];
static bool writable_snapshot_valid[
    USER_RUNTIME_PROCESS_COUNT
][USER_RUNTIME_MAX_IMAGE_PAGES];
static struct micros_endpoint_registry *endpoint_registry;
static struct micros_grant_registry *grant_registry;
static uint64_t baseline_owned;
static uint64_t baseline_free;
static size_t baseline_processes;
static size_t baseline_threads;
static size_t baseline_harts;
static bool rodata_fault_seen;
static bool loader_validated;
static bool final_client_send;
static size_t returned_actor = USER_RUNTIME_PROCESS_COUNT;

static void clear_bytes(void *storage, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static void fill_bytes(void *storage, unsigned char value, size_t size)
{
    unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = value;
    }
}

static void copy_bytes(void *destination, const void *source, size_t size)
{
    unsigned char *target = destination;
    const unsigned char *input = source;
    size_t index;

    for (index = 0; index < size; ++index) {
        target[index] = input[index];
    }
}

static bool bytes_equal(
    const void *left,
    const void *right,
    size_t size
)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (left_bytes[index] != right_bytes[index]) {
            return false;
        }
    }
    return true;
}

static bool bytes_are_zero(const void *storage, size_t size)
{
    const unsigned char *bytes = storage;
    size_t index;

    for (index = 0; index < size; ++index) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static uint64_t read_satp(void)
{
    uint64_t value;

    __asm__ volatile("csrr %0, satp" : "=r"(value));
    return value;
}

static uint32_t segment_permissions(uint32_t flags)
{
    uint32_t permissions = 0;

    if ((flags & USER_RUNTIME_PROGRAM_READ) != 0) {
        permissions |= MICROS_SV39_PERMISSION_READ;
    }
    if ((flags & USER_RUNTIME_PROGRAM_WRITE) != 0) {
        permissions |= MICROS_SV39_PERMISSION_WRITE;
    }
    if ((flags & USER_RUNTIME_PROGRAM_EXECUTE) != 0) {
        permissions |= MICROS_SV39_PERMISSION_EXECUTE;
    }
    return permissions;
}

static struct mapped_page *find_page(
    size_t actor,
    uint64_t virtual_address
)
{
    size_t index;

    for (index = 0; index < images[actor].page_count; ++index) {
        if (
            images[actor].pages[index].virtual_address
                == virtual_address
        ) {
            return &images[actor].pages[index];
        }
    }
    return NULL;
}

static bool allocate_image_pages(size_t actor)
{
    size_t segment_index;

    for (
        segment_index = 0;
        segment_index < micros_user_runtime_test_image.segment_count;
        ++segment_index
    ) {
        const struct micros_user_runtime_image_segment *segment =
            &micros_user_runtime_test_image.segments[segment_index];
        uint32_t permissions = segment_permissions(segment->flags);
        uint64_t offset;

        if (
            segment->virtual_address % MICROS_SV39_PAGE_SIZE != 0
            || segment->memory_size == 0
            || segment->memory_size % MICROS_SV39_PAGE_SIZE != 0
            || permissions == 0
            || (
                permissions & MICROS_SV39_PERMISSION_WRITE
                && permissions & MICROS_SV39_PERMISSION_EXECUTE
            )
        ) {
            return false;
        }
        for (
            offset = 0;
            offset < segment->memory_size;
            offset += MICROS_SV39_PAGE_SIZE
        ) {
            struct mapped_page *page;

            if (
                images[actor].page_count
                    >= USER_RUNTIME_MAX_IMAGE_PAGES
            ) {
                return false;
            }
            page = &images[actor].pages[images[actor].page_count];
            page->virtual_address =
                segment->virtual_address + offset;
            page->permissions = permissions;
            page->segment_index = segment_index;
            if (
                micros_user_address_space_allocate_page(
                    processes[actor],
                    page->virtual_address,
                    permissions,
                    &page->physical_address
                ) != MICROS_USER_ADDRESS_SPACE_OK
            ) {
                return false;
            }
            fill_bytes(
                (void *)(uintptr_t)page->physical_address,
                (unsigned char)(UINT8_C(0xc0) + actor),
                MICROS_SV39_PAGE_SIZE
            );
            clear_bytes(
                (void *)(uintptr_t)page->physical_address,
                MICROS_SV39_PAGE_SIZE
            );
            ++images[actor].page_count;
        }
    }
    return true;
}

static bool copy_image_segments(size_t actor)
{
    size_t segment_index;

    for (
        segment_index = 0;
        segment_index < micros_user_runtime_test_image.segment_count;
        ++segment_index
    ) {
        const struct micros_user_runtime_image_segment *segment =
            &micros_user_runtime_test_image.segments[segment_index];
        uint64_t copied = 0;

        while (copied < segment->file_size) {
            uint64_t virtual_address =
                segment->virtual_address + copied;
            uint64_t page_address =
                virtual_address
                & ~(uint64_t)(MICROS_SV39_PAGE_SIZE - 1);
            size_t page_offset =
                (size_t)(virtual_address - page_address);
            size_t chunk = MICROS_SV39_PAGE_SIZE - page_offset;
            struct mapped_page *page = find_page(actor, page_address);

            if (segment->file_size - copied < chunk) {
                chunk = (size_t)(segment->file_size - copied);
            }
            if (page == NULL) {
                return false;
            }
            copy_bytes(
                (void *)(uintptr_t)(
                    page->physical_address + page_offset
                ),
                segment->file_bytes + copied,
                chunk
            );
            copied += chunk;
        }
    }
    return true;
}

static bool verify_loaded_pages(size_t actor)
{
    size_t page_index;

    for (
        page_index = 0;
        page_index < images[actor].page_count;
        ++page_index
    ) {
        const struct mapped_page *page =
            &images[actor].pages[page_index];
        const struct micros_user_runtime_image_segment *segment =
            &micros_user_runtime_test_image.segments[
                page->segment_index
            ];
        const unsigned char *bytes =
            (const unsigned char *)(uintptr_t)page->physical_address;
        uint64_t segment_file_end =
            segment->virtual_address + segment->file_size;
        size_t offset;
        uint64_t physical;
        uint32_t permissions;

        if (
            micros_user_address_space_lookup(
                processes[actor],
                page->virtual_address,
                &physical,
                &permissions
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || physical != page->physical_address
            || permissions != page->permissions
        ) {
            return false;
        }
        for (offset = 0; offset < MICROS_SV39_PAGE_SIZE; ++offset) {
            uint64_t address = page->virtual_address + offset;
            unsigned char expected = 0;

            if (address < segment_file_end) {
                expected = segment->file_bytes[
                    address - segment->virtual_address
                ];
            }
            if (bytes[offset] != expected) {
                return false;
            }
        }
    }
    return true;
}

static bool translate_range(
    size_t actor,
    uint64_t address,
    size_t size,
    uint32_t required_permissions,
    uint64_t *physical
)
{
    uint32_t permissions;
    size_t contiguous;

    return (
        micros_user_address_space_translate(
            processes[actor],
            address,
            physical,
            &permissions,
            &contiguous
        ) == MICROS_USER_ADDRESS_SPACE_OK
        && contiguous >= size
        && (permissions & required_permissions)
            == required_permissions
    );
}

static bool patch_configuration(size_t actor)
{
    struct micros_user_runtime_test_config config = {
        .version = MICROS_USER_RUNTIME_TEST_VERSION,
        .role = actor + 1,
        .client_endpoint = endpoints[USER_RUNTIME_CLIENT],
        .server_endpoint = endpoints[USER_RUNTIME_SERVER],
        .peer_endpoint = endpoints[USER_RUNTIME_PEER],
    };
    uint64_t physical;

    if (
        !translate_range(
            actor,
            micros_user_runtime_test_image.config_address,
            sizeof(config),
            MICROS_SV39_PERMISSION_WRITE,
            &physical
        )
    ) {
        return false;
    }
    copy_bytes(
        (void *)(uintptr_t)physical,
        &config,
        sizeof(config)
    );
    return true;
}

static bool snapshot_writable_pages(size_t actor)
{
    size_t page_index;

    for (
        page_index = 0;
        page_index < images[actor].page_count;
        ++page_index
    ) {
        const struct mapped_page *page =
            &images[actor].pages[page_index];

        if (
            (
                page->permissions
                & MICROS_SV39_PERMISSION_WRITE
            ) == 0
        ) {
            continue;
        }
        copy_bytes(
            writable_snapshots[actor][page_index],
            (const void *)(uintptr_t)page->physical_address,
            MICROS_SV39_PAGE_SIZE
        );
        writable_snapshot_valid[actor][page_index] = true;
    }
    return true;
}

static bool writable_pages_match_snapshot(size_t actor)
{
    size_t page_index;

    for (
        page_index = 0;
        page_index < images[actor].page_count;
        ++page_index
    ) {
        if (
            writable_snapshot_valid[actor][page_index]
            && !bytes_equal(
                (const void *)(uintptr_t)
                    images[actor].pages[page_index].physical_address,
                writable_snapshots[actor][page_index],
                MICROS_SV39_PAGE_SIZE
            )
        ) {
            return false;
        }
    }
    return true;
}

static bool prepare_process(
    struct micros_kernel_objects *objects,
    size_t actor
)
{
    struct micros_user_context context;
    uint64_t stack_address =
        MICROS_USER_VIRTUAL_END - MICROS_SV39_PAGE_SIZE;

    if (
        micros_user_address_space_create(processes[actor])
            != MICROS_USER_ADDRESS_SPACE_OK
        || !allocate_image_pages(actor)
        || !copy_image_segments(actor)
        || !verify_loaded_pages(actor)
        || micros_user_address_space_allocate_page(
            processes[actor],
            stack_address,
            MICROS_SV39_PERMISSION_READ
                | MICROS_SV39_PERMISSION_WRITE,
            &images[actor].stack_physical
        ) != MICROS_USER_ADDRESS_SPACE_OK
    ) {
        return false;
    }
    fill_bytes(
        (void *)(uintptr_t)images[actor].stack_physical,
        (unsigned char)(UINT8_C(0xd0) + actor),
        MICROS_SV39_PAGE_SIZE
    );
    clear_bytes(
        (void *)(uintptr_t)images[actor].stack_physical,
        MICROS_SV39_PAGE_SIZE
    );
    if (
        !bytes_are_zero(
            (const void *)(uintptr_t)images[actor].stack_physical,
            MICROS_SV39_PAGE_SIZE
        )
        || !patch_configuration(actor)
        || !snapshot_writable_pages(actor)
        || micros_thread_create(
            objects,
            processes[actor],
            &threads[actor]
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    clear_bytes(&context, sizeof(context));
    context.ra = UINT64_C(0x7111111111111111) + actor;
    context.sp = MICROS_USER_VIRTUAL_END;
    context.gp = UINT64_C(0x3333333333333333);
    context.tp = UINT64_C(0x4444444444444444);
    context.t0 = UINT64_C(0x7555555555555555) + actor;
    context.s0 = UINT64_C(0x7666666666666666) + actor;
    context.a0 = UINT64_C(0x7777777777777777) + actor;
    context.a7 = UINT64_C(0x7888888888888888) + actor;
    context.sstatus = 0;
    context.sepc = micros_user_runtime_test_image.entry;
    return (
        micros_user_execution_prepare(threads[actor], &context)
            == MICROS_USER_EXECUTION_OK
    );
}

static size_t current_actor(const struct micros_hart *hart)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    struct micros_thread_handle current;
    size_t actor;

    if (
        objects == NULL
        || hart == NULL
        || micros_hart_current_thread(
            objects,
            micros_kernel_object_runtime_boot_hart_handle(),
            &current
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return USER_RUNTIME_PROCESS_COUNT;
    }
    for (actor = 0; actor < USER_RUNTIME_PROCESS_COUNT; ++actor) {
        if (
            current.slot == threads[actor].slot
            && current.generation == threads[actor].generation
        ) {
            return actor;
        }
    }
    return USER_RUNTIME_PROCESS_COUNT;
}

static bool read_configuration(
    size_t actor,
    struct micros_user_runtime_test_config *config
)
{
    uint64_t physical;

    if (
        config == NULL
        || !translate_range(
            actor,
            micros_user_runtime_test_image.config_address,
            sizeof(*config),
            MICROS_SV39_PERMISSION_READ,
            &physical
        )
    ) {
        return false;
    }
    copy_bytes(
        config,
        (const void *)(uintptr_t)physical,
        sizeof(*config)
    );
    return true;
}

static bool configurations_complete(void)
{
    static const uint64_t role_status[USER_RUNTIME_PROCESS_COUNT] = {
        MICROS_USER_RUNTIME_TEST_STATUS_STARTUP
            | MICROS_USER_RUNTIME_TEST_STATUS_STACK
            | MICROS_USER_RUNTIME_TEST_STATUS_MEMORY
            | MICROS_USER_RUNTIME_TEST_STATUS_REGISTERS
            | MICROS_USER_RUNTIME_TEST_STATUS_IPC
            | MICROS_USER_RUNTIME_TEST_STATUS_GRANTS
            | MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE,
        MICROS_USER_RUNTIME_TEST_STATUS_STARTUP
            | MICROS_USER_RUNTIME_TEST_STATUS_STACK
            | MICROS_USER_RUNTIME_TEST_STATUS_MEMORY
            | MICROS_USER_RUNTIME_TEST_STATUS_IPC
            | MICROS_USER_RUNTIME_TEST_STATUS_GRANTS
            | MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE,
        MICROS_USER_RUNTIME_TEST_STATUS_STARTUP
            | MICROS_USER_RUNTIME_TEST_STATUS_STACK
            | MICROS_USER_RUNTIME_TEST_STATUS_MEMORY
            | MICROS_USER_RUNTIME_TEST_STATUS_IPC
            | MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE,
    };
    size_t actor;

    for (actor = 0; actor < USER_RUNTIME_PROCESS_COUNT; ++actor) {
        struct micros_user_runtime_test_config config;

        if (
            !read_configuration(actor, &config)
            || config.version != MICROS_USER_RUNTIME_TEST_VERSION
            || config.role != actor + 1
            || config.client_endpoint != endpoints[USER_RUNTIME_CLIENT]
            || config.server_endpoint != endpoints[USER_RUNTIME_SERVER]
            || config.peer_endpoint != endpoints[USER_RUNTIME_PEER]
            || config.status != role_status[actor]
            || config.failure != 0
        ) {
            return false;
        }
    }
    return true;
}

static bool hold_other_threads(
    struct micros_kernel_objects *objects,
    size_t current
)
{
    size_t actor;

    for (actor = 0; actor < USER_RUNTIME_PROCESS_COUNT; ++actor) {
        const struct micros_thread *thread;

        if (actor == current) {
            continue;
        }
        if (
            micros_thread_resolve(
                objects,
                threads[actor],
                &thread
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
        if (
            (
                thread->runtime_flags
                & MICROS_THREAD_RTS_INACTIVE
            ) == 0
            && micros_thread_runtime_flags_set(
                objects,
                threads[actor],
                MICROS_THREAD_RTS_INACTIVE
            ) != MICROS_KERNEL_OBJECT_OK
        ) {
            return false;
        }
    }
    return true;
}

bool micros_user_runtime_test_before_ecall(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_user_runtime_test_config config;
    size_t actor = current_actor(hart);

    final_client_send = false;
    if (
        frame == NULL
        || actor >= USER_RUNTIME_PROCESS_COUNT
    ) {
        return false;
    }
    if (
        actor == USER_RUNTIME_CLIENT
        && frame->a7 == MICROS_SYSCALL_ABI_SEND
        && read_configuration(actor, &config)
        && (
            config.status
            & MICROS_USER_RUNTIME_TEST_STATUS_COMPLETE
        ) != 0
    ) {
        final_client_send = true;
    }
    return true;
}

bool micros_user_runtime_test_after_dispatch(
    struct micros_hart *hart,
    struct micros_trap_frame *frame,
    enum micros_syscall_return syscall_return
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();

    (void)hart;
    (void)frame;
    if (!final_client_send) {
        return true;
    }
    final_client_send = false;
    return (
        objects != NULL
        && syscall_return == MICROS_SYSCALL_RETURN_CAPTURED
        && micros_thread_install_policy(
            objects,
            threads[USER_RUNTIME_SERVER],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER + 1,
            UINT64_MAX
        ) == MICROS_KERNEL_OBJECT_OK
    );
}

enum micros_user_runtime_test_trap_result
micros_user_runtime_test_handle_trap(
    struct micros_hart *hart,
    struct micros_trap_frame *frame
)
{
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    const uint64_t control_mask =
        MICROS_RISCV_SSTATUS_SIE
        | MICROS_RISCV_SSTATUS_SPIE
        | MICROS_RISCV_SSTATUS_SPP
        | MICROS_RISCV_SSTATUS_SUM
        | TEST_SSTATUS_UBE
        | TEST_SSTATUS_VS
        | TEST_SSTATUS_FS
        | TEST_SSTATUS_XS
        | TEST_SSTATUS_MXR
        | TEST_SSTATUS_SD;
    uint64_t cause;
    size_t actor = current_actor(hart);

    if (
        objects == NULL
        || frame == NULL
        || actor >= USER_RUNTIME_PROCESS_COUNT
        || (frame->scause & (UINT64_C(1) << 63)) != 0
    ) {
        return MICROS_USER_RUNTIME_TEST_TRAP_MISMATCH;
    }
    cause = frame->scause & ((UINT64_C(1) << 63) - 1);
    if (
        cause == USER_RUNTIME_EXCEPTION_STORE_PAGE_FAULT
        && actor == USER_RUNTIME_SERVER
        && !rodata_fault_seen
        && frame->sepc
            == micros_user_runtime_test_image.rodata_fault_address
        && frame->stval
            == micros_user_runtime_test_image.rodata_address
        && writable_pages_match_snapshot(actor)
    ) {
        frame->sepc =
            micros_user_runtime_test_image.rodata_resume_address;
        rodata_fault_seen = true;
        return MICROS_USER_RUNTIME_TEST_TRAP_USER_RETURN;
    }
    if (
        cause != USER_RUNTIME_EXCEPTION_BREAKPOINT
        || frame->sepc
            != micros_user_runtime_test_image.service_returned_address
    ) {
        return MICROS_USER_RUNTIME_TEST_TRAP_MISMATCH;
    }
    returned_actor = actor;
    if (
        !hold_other_threads(objects, actor)
        || micros_scheduler_test_prepare_supervisor_return(
            hart,
            frame
        ) != MICROS_SCHEDULER_OK
    ) {
        return MICROS_USER_RUNTIME_TEST_TRAP_MISMATCH;
    }
    frame->sp = micros_user_runtime_test_saved_sp;
    frame->sepc =
        (uintptr_t)micros_user_runtime_test_supervisor_resume;
    frame->sstatus &= ~control_mask;
    frame->sstatus |= MICROS_RISCV_SSTATUS_SPP;
    return MICROS_USER_RUNTIME_TEST_TRAP_SUPERVISOR_RETURN;
}

static bool close_endpoint(
    struct micros_kernel_objects *objects,
    size_t actor
)
{
    struct micros_grant_cancel_plan plan;
    enum micros_grant_error grant_error;
    enum micros_ipc_error ipc_error;

    grant_error = micros_grant_prepare_endpoint_cancel(
        grant_registry,
        endpoint_registry,
        objects,
        endpoints[actor],
        &plan
    );
    if (grant_error != MICROS_GRANT_OK) {
        uart_write("MICROS_USER_RUNTIME_CLOSE grant-prepare=");
        uart_write_hex64(grant_error);
        uart_write("\n");
        return false;
    }
    ipc_error = micros_ipc_endpoint_close(
        endpoint_registry,
        objects,
        endpoints[actor]
    );
    if (ipc_error != MICROS_IPC_OK) {
        const struct micros_thread *thread =
            &objects->threads[threads[actor].slot];

        uart_write("MICROS_USER_RUNTIME_CLOSE ipc=");
        uart_write_hex64(ipc_error);
        uart_write(" actor=");
        uart_write_hex64(actor);
        uart_write(" flags=");
        uart_write_hex64(thread->runtime_flags);
        uart_write(" queue=");
        uart_write_hex64(thread->ipc_queue_kind);
        uart_write(" pending=");
        uart_write_hex64(thread->ipc_delivery_pending);
        uart_write(" result=");
        uart_write_hex64(thread->ipc_staged_result);
        uart_write("\n");
        return false;
    }
    grant_error = micros_grant_commit_endpoint_cancel(
        grant_registry,
        &plan
    );
    if (grant_error != MICROS_GRANT_OK) {
        uart_write("MICROS_USER_RUNTIME_CLOSE grant-commit=");
        uart_write_hex64(grant_error);
        uart_write("\n");
        return false;
    }
    return true;
}

static bool release_process(
    struct micros_kernel_objects *objects,
    size_t actor
)
{
    uint64_t released;
    size_t page_index;

    if (
        micros_thread_scheduler_remove(
            objects,
            threads[actor]
        ) != MICROS_KERNEL_OBJECT_OK
        || micros_user_execution_detach(threads[actor])
            != MICROS_USER_EXECUTION_OK
        || micros_thread_release(objects, threads[actor])
            != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    for (
        page_index = 0;
        page_index < images[actor].page_count;
        ++page_index
    ) {
        if (
            micros_user_address_space_release_page(
                processes[actor],
                images[actor].pages[page_index].virtual_address,
                &released
            ) != MICROS_USER_ADDRESS_SPACE_OK
            || released
                != images[actor].pages[page_index].physical_address
        ) {
            return false;
        }
    }
    if (
        micros_user_address_space_release_page(
            processes[actor],
            MICROS_USER_VIRTUAL_END - MICROS_SV39_PAGE_SIZE,
            &released
        ) != MICROS_USER_ADDRESS_SPACE_OK
        || released != images[actor].stack_physical
        || micros_user_address_space_destroy(processes[actor])
            != MICROS_USER_ADDRESS_SPACE_OK
        || micros_frame_ownership_runtime_release_process(
            objects,
            processes[actor]
        ) != MICROS_KERNEL_OBJECT_OK
    ) {
        return false;
    }
    return true;
}

static bool endpoint_registry_is_empty(void)
{
    size_t index;

    for (index = 0; index < MICROS_PROCESS_CAPACITY; ++index) {
        if (
            endpoint_registry->endpoints[index].state
                != MICROS_ENDPOINT_STATE_FREE
        ) {
            return false;
        }
    }
    return true;
}

static bool scheduler_is_idle(
    const struct micros_kernel_objects *objects
)
{
    const struct micros_hart *hart =
        &objects->harts[
            micros_kernel_object_runtime_boot_hart_handle().slot
        ];
    size_t priority;

    if (
        hart->current_thread.slot != 0
        || hart->current_thread.generation != 0
        || hart->trap.primary_stack_bottom
            != hart->idle_primary_stack_bottom
        || hart->trap.primary_stack_top
            != hart->idle_primary_stack_top
    ) {
        return false;
    }
    for (
        priority = 0;
        priority < MICROS_SCHEDULER_PRIORITY_COUNT;
        ++priority
    ) {
        if (
            hart->ready_head[priority].slot != 0
            || hart->ready_head[priority].generation != 0
            || hart->ready_tail[priority].slot != 0
            || hart->ready_tail[priority].generation != 0
        ) {
            return false;
        }
    }
    return true;
}

_Noreturn void micros_user_runtime_test_finish(void)
{
    const struct micros_frame_ownership *ledger =
        micros_frame_ownership_runtime_ledger();
    const struct micros_kernel_address_space_report *kernel_space =
        micros_kernel_address_space_report();
    struct micros_kernel_objects *objects =
        micros_kernel_object_runtime_test_registry();
    size_t actor;
    uint64_t failure_stage = 1;

    if (
        ledger == NULL
        || kernel_space == NULL
        || objects == NULL
        || endpoint_registry == NULL
        || grant_registry == NULL
        || !loader_validated
        || !rodata_fault_seen
        || returned_actor != USER_RUNTIME_SERVER
        || !configurations_complete()
    ) {
        goto failure;
    }
    for (actor = 0; actor < USER_RUNTIME_PROCESS_COUNT; ++actor) {
        if (!close_endpoint(objects, actor)) {
            failure_stage = UINT64_C(0x10) + actor;
            goto failure;
        }
    }
    failure_stage = 2;
    for (actor = 0; actor < USER_RUNTIME_PROCESS_COUNT; ++actor) {
        if (!release_process(objects, actor)) {
            failure_stage = UINT64_C(0x20) + actor;
            goto failure;
        }
    }
    failure_stage = 3;
    if (
        grant_registry->active_count != 0
        || !endpoint_registry_is_empty()
        || objects->live_process_count != baseline_processes
        || objects->live_thread_count != baseline_threads
        || objects->registered_hart_count != baseline_harts
        || ledger->owned_frame_count != baseline_owned
        || ledger->allocator->free_frame_count != baseline_free
        || !scheduler_is_idle(objects)
        || read_satp()
            != (
                MICROS_RISCV_SATP_MODE_SV39
                | (kernel_space->root_physical_address >> 12)
            )
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto failure;
    }
    uart_write(
        "MICROS_USER_RUNTIME_TEST_PASS "
        "elf=freestanding startup=validated syscalls=1-10 "
        "registers=preserved stack=external data=initialized "
        "bss=zero rodata=protected return=trapped cleanup=complete\n"
    );
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_NONE
    );

failure:
    uart_write("MICROS_TEST_FAILURE user-runtime stage=");
    uart_write_hex64(failure_stage);
    uart_write(" actor=");
    uart_write_hex64(returned_actor);
    uart_write("\n");
    for (actor = 0; actor < USER_RUNTIME_PROCESS_COUNT; ++actor) {
        struct micros_user_runtime_test_config config;

        if (read_configuration(actor, &config)) {
            uart_write("MICROS_USER_RUNTIME_STATE actor=");
            uart_write_hex64(actor);
            uart_write(" status=");
            uart_write_hex64(config.status);
            uart_write(" failure=");
            uart_write_hex64(config.failure);
            uart_write(" client=");
            uart_write_hex64(config.client_endpoint);
            uart_write(" server=");
            uart_write_hex64(config.server_endpoint);
            uart_write(" peer=");
            uart_write_hex64(config.peer_endpoint);
            uart_write("\n");
        }
    }
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}

_Noreturn void micros_user_runtime_runtime_run_self_test(void)
{
    static const struct micros_privilege_profile profiles[] = {
        {
            .id = 1,
            .name = "RUNTIME_CLIENT",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_CALL,
            .call_targets = UINT32_C(1) << 2,
            .send_targets = UINT32_C(1) << 2,
        },
        {
            .id = 2,
            .name = "RUNTIME_SERVER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_SEND
                | MICROS_PRIVILEGE_OPERATION_REPLY
                | MICROS_PRIVILEGE_OPERATION_REPLY_RECEIVE,
            .send_targets =
                (UINT32_C(1) << 1)
                | (UINT32_C(1) << 3),
        },
        {
            .id = 3,
            .name = "RUNTIME_PEER",
            .operations =
                MICROS_PRIVILEGE_OPERATION_RECEIVE
                | MICROS_PRIVILEGE_OPERATION_NOTIFY,
            .notify_targets = UINT32_C(1) << 2,
        },
    };
    const struct micros_frame_ownership *ledger;
    struct micros_kernel_objects *objects;
    size_t actor;
    uintptr_t saved_status = riscv_irq_save();

    ledger = micros_frame_ownership_runtime_ledger();
    objects = micros_kernel_object_runtime_test_registry();
    if (
        ledger == NULL
        || objects == NULL
        || micros_user_runtime_test_image.entry
            != MICROS_USER_VIRTUAL_BASE
        || micros_user_runtime_test_image.segment_count
            != MICROS_USER_RUNTIME_IMAGE_SEGMENT_COUNT
        || micros_user_runtime_test_image.image_end
            > MICROS_USER_VIRTUAL_END - MICROS_SV39_PAGE_SIZE
    ) {
        goto failure;
    }
    baseline_owned = ledger->owned_frame_count;
    baseline_free = ledger->allocator->free_frame_count;
    baseline_processes = objects->live_process_count;
    baseline_threads = objects->live_thread_count;
    baseline_harts = objects->registered_hart_count;
    if (
        micros_ipc_runtime_initialize(
            profiles,
            sizeof(profiles) / sizeof(profiles[0])
        ) != MICROS_ENDPOINT_OK
        || micros_grant_runtime_initialize() != MICROS_GRANT_OK
        || (
            endpoint_registry =
                micros_ipc_runtime_authoritative_registry()
        ) == NULL
        || (
            grant_registry =
                micros_grant_runtime_authoritative_registry()
        ) == NULL
    ) {
        goto failure;
    }
    for (actor = 0; actor < USER_RUNTIME_PROCESS_COUNT; ++actor) {
        if (
            micros_process_create(objects, &processes[actor])
                != MICROS_KERNEL_OBJECT_OK
            || micros_endpoint_reserve(
                endpoint_registry,
                objects,
                processes[actor],
                &endpoints[actor]
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_install_profile(
                endpoint_registry,
                objects,
                processes[actor],
                (uint8_t)(actor + 1)
            ) != MICROS_ENDPOINT_OK
            || micros_endpoint_activate(
                endpoint_registry,
                objects,
                endpoints[actor]
            ) != MICROS_ENDPOINT_OK
        ) {
            goto failure;
        }
    }
    for (actor = 0; actor < USER_RUNTIME_PROCESS_COUNT; ++actor) {
        if (!prepare_process(objects, actor)) {
            goto failure;
        }
    }
    loader_validated = true;
    __asm__ volatile("fence.i" : : : "memory");
    if (
        micros_scheduler_initialize(USER_RUNTIME_TIMER_INTERVAL)
            != MICROS_SCHEDULER_OK
        || micros_scheduler_admit(
            threads[USER_RUNTIME_SERVER],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            UINT64_MAX
        ) != MICROS_SCHEDULER_OK
        || micros_scheduler_admit(
            threads[USER_RUNTIME_PEER],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            UINT64_MAX
        ) != MICROS_SCHEDULER_OK
        || micros_scheduler_admit(
            threads[USER_RUNTIME_CLIENT],
            MICROS_SCHEDULER_PRIORITY_DEFAULT_USER,
            UINT64_MAX
        ) != MICROS_SCHEDULER_OK
        || micros_grant_runtime_validate() != MICROS_GRANT_OK
        || micros_ipc_runtime_validate() != MICROS_ENDPOINT_OK
        || micros_kernel_objects_validate(objects)
            != MICROS_KERNEL_OBJECT_OK
        || micros_frame_ownership_runtime_validate(objects)
            != MICROS_FRAME_OWNERSHIP_OK
    ) {
        goto failure;
    }
    __asm__ volatile(
        "mv %0, sp\n"
        "mv %1, gp\n"
        "mv %2, tp"
        : "=r"(micros_user_runtime_test_saved_sp),
          "=r"(micros_user_runtime_test_saved_gp),
          "=r"(micros_user_runtime_test_saved_tp)
    );
    (void)saved_status;
    (void)micros_scheduler_start();

failure:
    riscv_irq_restore(saved_status);
    uart_write("MICROS_TEST_FAILURE user-runtime-setup\n");
    uart_flush();
    (void)sbi_system_reset(
        SBI_RESET_TYPE_SHUTDOWN,
        SBI_RESET_REASON_SYSTEM_FAILURE
    );
    for (;;) {
        __asm__ volatile("wfi");
    }
}
