#define _DEFAULT_SOURCE 1

#include "selected_address_space_test.h"

#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#if defined(MICROS_SELECTED_ADDRESS_SPACE_LAUNCHER)
#include <unistd.h>
#endif

#if defined(MICROS_SELECTED_ADDRESS_SPACE_BASELINE) && MICROS_SELECTED_ADDRESS_SPACE_BASELINE != 1
#error "MEM04 baseline must be explicitly equal to 1"
#endif
#if defined(MICROS_TEST_WORK_COUNTS)
#if !defined(MICROS_AS_NATIVE_CHANNEL) || MICROS_AS_NATIVE_CHANNEL < 0 || MICROS_AS_NATIVE_CHANNEL > 2
#error "MEM04 observations require one exact declared native channel"
#endif
#endif

#include "arch/riscv64/mmu.h"
#include "arch/riscv64/trap_context.h"
#include "kernel/memory/user_address_space_internal.h"
#include "kernel/objects/kernel_object_runtime_internal.h"
#include "kernel/scheduler/scheduler_runtime_internal.h"
#include "kernel/tty/tty_handoff_runtime_internal.h"
#include "kernel/vm/vm_handoff_runtime.h"
#include "micros/frame_ownership_runtime.h"
#include "micros/kernel_object_runtime.h"
#include "micros/panic.h"
#include "micros/scheduler_core.h"

#define REQUIRE(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return false; \
        } \
    } while (false)

#define TEST_SENTINEL UINT64_C(0xfeedfacefeedface)
#define TEST_PRODUCER UINT64_C(0x4d454d3034000000)
#define TEST_OWNER_MASK UINT32_C(11388)
#define ARENA_BYTES \
    (((size_t)MICROS_FRAME_ALLOCATOR_MAX_MANAGED_FRAMES + 16) * MICROS_FRAME_SIZE)

enum query_operation {
    QUERY_LOOKUP,
    QUERY_TRANSLATE,
    QUERY_ACTIVATE,
    QUERY_DIAGNOSE,
    QUERY_ALLOCATE,
    QUERY_SCHEDULER,
    QUERY_PREPARE,
};

struct selected_address_space_context {
    unsigned char *arena;
    uintptr_t status;
    uint64_t satp;
    uint64_t satp_writes;
    uint64_t capture;
    uint64_t frames;
    size_t ranges;
    size_t density;
    unsigned int scenario;
    size_t allocation_limit;
    bool active;
    bool null_output;
    uint32_t allocation_permissions;
    struct micros_physical_range memory[16];
    struct micros_process_handle process;
    struct micros_process_handle foreign_process;
    uint64_t foreign_physical;
    struct micros_thread_handle thread;
    _Alignas(16) unsigned char thread_stack[MICROS_THREAD_KERNEL_STACK_SIZE];
    struct micros_bootstrap_control_state bootstrap;
    struct micros_bootstrap_control_state bootstrap_before;
    struct scheduler_runtime_context scheduler;
    struct scheduler_runtime_context scheduler_before;
    struct micros_frame_allocator allocator_before;
    struct micros_frame_ownership ownership_before;
    struct micros_kernel_objects objects_before;
    struct micros_endpoint_registry endpoints_before;
    struct micros_vm_handoff_state vm_before;
    struct tty_handoff_context tty_before;
    struct micros_kernel_address_space_report kernel_before;
    bool kernel_ready_before;
    bool scratch_busy_before;
    uint64_t scratch_before[2][MICROS_FRAME_ALLOCATOR_BITMAP_WORDS];
#if !defined(MICROS_SELECTED_ADDRESS_SPACE_BASELINE)
    struct micros_frame_selection selection_before[2];
    uint8_t scratch_mode_before;
#endif
    unsigned char kernel_page_before[MICROS_SV39_PAGE_SIZE];
    uint64_t table_addresses[3];
    size_t table_count;
    struct micros_user_address_space_page_table tables_before[3];
#if defined(MICROS_TEST_WORK_COUNTS)
    struct micros_test_work_snapshot observation;
#endif
};

static struct selected_address_space_context context;

uintptr_t micros_as_native_irq_save(void)
{
    uintptr_t saved = context.status;

    context.status &= ~(uintptr_t)MICROS_RISCV_SSTATUS_SIE;
    return saved;
}

void micros_as_native_irq_restore(uintptr_t status)
{
    context.status = (context.status & ~(uintptr_t)MICROS_RISCV_SSTATUS_SIE)
        | (status & MICROS_RISCV_SSTATUS_SIE);
}

bool micros_as_native_irq_enabled(void)
{
    return (context.status & MICROS_RISCV_SSTATUS_SIE) != 0;
}

uint64_t micros_as_native_read_satp(void)
{
    return context.satp;
}

uint64_t micros_riscv_activate_sv39(uint64_t root_physical_address)
{
    ++context.satp_writes;
    context.satp = MICROS_RISCV_SATP_MODE_SV39 | (root_physical_address >> 12);
    return context.satp;
}

void micros_riscv_publish_page_table(void)
{
}

void micros_riscv_publish_page_table_address(uint64_t virtual_address)
{
    (void)virtual_address;
}

_Noreturn void micros_panic_entry(
    uintptr_t hart_id,
    const char *reason,
    const char *file,
    uint32_t line
)
{
    fprintf(stderr, "unexpected native panic hart=%" PRIuPTR " %s at %s:%" PRIu32 "\n",
            hart_id, reason, file, line);
    exit(3);
}

_Noreturn void micros_trap_panic_entry(
    uintptr_t hart_id,
    const char *reason,
    const char *file,
    uint32_t line,
    const struct micros_trap_frame *frame
)
{
    (void)frame;
    micros_panic_entry(hart_id, reason, file, line);
}

static bool initialize_geometry(uint64_t frames, size_t ranges)
{
    struct micros_frame_allocator *allocator = micros_as_native_allocator();
    struct micros_kernel_objects *objects;
    struct micros_kernel_address_space_report report = {0};
    size_t index;
    struct micros_frame_owner kernel_owner = {
        .kind = MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE,
    };

    context.status = 0;
    context.satp = 0;
    context.frames = frames;
    context.ranges = ranges;
    context.density = 0;
    context.scenario = 0;
    context.allocation_limit = SIZE_MAX;
    context.active = false;
    context.null_output = false;
    context.allocation_permissions = MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE;
    context.table_count = 0;
    context.thread = (struct micros_thread_handle){0};
    context.foreign_process = (struct micros_process_handle){0};
    context.foreign_physical = 0;
    memset(&context.bootstrap, 0, sizeof(context.bootstrap));
    memset(&context.scheduler, 0, sizeof(context.scheduler));
    micros_as_native_bootstrap_reset();
    micros_as_native_objects_reset();
    micros_as_native_kernel_reset();
    micros_as_native_ipc_reset();
    micros_as_native_vm_reset();
    memset(tty_handoff_context(), 0, sizeof(*tty_handoff_context()));
    micros_user_address_space_test_fail_after_allocations(SIZE_MAX);
    REQUIRE(ranges != 0 && ranges <= 16 && frames % ranges == 0);
    for (index = 0; index < ranges; ++index) {
        context.memory[ranges - index - 1] = (struct micros_physical_range){
            (uint64_t)(uintptr_t)context.arena
                + index * (frames / ranges + 1) * MICROS_FRAME_SIZE,
            (frames / ranges) * MICROS_FRAME_SIZE,
        };
    }
    REQUIRE(micros_frame_allocator_initialize(
        allocator, context.memory, ranges, NULL, 0
    ) == MICROS_FRAME_ALLOCATOR_OK);
    REQUIRE(micros_frame_ownership_runtime_initialize() == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(micros_kernel_object_runtime_initialize(0) == MICROS_KERNEL_OBJECT_OK);
    objects = micros_kernel_object_runtime_authoritative_registry();
    REQUIRE(objects != NULL);
    REQUIRE(micros_frame_ownership_runtime_allocate(
        kernel_owner, &report.root_physical_address
    ) == MICROS_FRAME_OWNERSHIP_OK);
    micros_user_address_space_clear_page(report.root_physical_address);
    micros_as_native_kernel_bind(&report);
    REQUIRE(micros_process_create_at(objects, 0, &context.process) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_user_address_space_create(context.process) == MICROS_USER_ADDRESS_SPACE_OK);
    REQUIRE(micros_user_address_space_activate_kernel() == MICROS_USER_ADDRESS_SPACE_OK);
    return true;
}

static bool initialize(uint64_t frames)
{
    return initialize_geometry(frames, 1);
}

static void save_state(void)
{
    const struct micros_kernel_address_space_report *kernel = micros_kernel_address_space_report();
    size_t index;

    context.allocator_before = *micros_as_native_allocator();
    context.ownership_before = *micros_as_native_ownership();
    context.objects_before = *micros_kernel_object_runtime_authoritative_registry();
    context.endpoints_before = *micros_as_native_ipc_input();
    context.vm_before = *micros_as_native_vm_input();
    context.tty_before = *tty_handoff_context();
    context.bootstrap_before = context.bootstrap;
    context.scheduler_before = context.scheduler;
    context.kernel_ready_before = kernel != NULL;
    if (kernel != NULL) {
        context.kernel_before = *kernel;
    }
    memcpy(context.kernel_page_before, context.arena, sizeof(context.kernel_page_before));
    context.scratch_busy_before = micros_as_native_scratch_busy();
#if !defined(MICROS_SELECTED_ADDRESS_SPACE_BASELINE)
    context.scratch_mode_before = micros_as_native_scratch_mode();
#endif
    for (index = 0; index < 2; ++index) {
        memcpy(context.scratch_before[index], micros_as_native_scratch_bitmap(index != 0),
               sizeof(context.scratch_before[index]));
#if !defined(MICROS_SELECTED_ADDRESS_SPACE_BASELINE)
        context.selection_before[index] = *micros_as_native_scratch_selection(index != 0);
#endif
    }
    for (index = 0; index < context.table_count; ++index) {
        memcpy(&context.tables_before[index],
               (const void *)(uintptr_t)context.table_addresses[index],
               MICROS_SV39_PAGE_SIZE);
    }
}

static bool state_matches(bool include_cursor)
{
    const struct micros_kernel_address_space_report *kernel = micros_kernel_address_space_report();
    const unsigned char *allocator =
        (const unsigned char *)micros_as_native_allocator();
    const unsigned char *expected = (const unsigned char *)&context.allocator_before;
    const size_t cursor_offset = offsetof(struct micros_frame_allocator, allocation_cursor);
    const size_t following = cursor_offset + sizeof(context.allocator_before.allocation_cursor);
    size_t index;

    if (memcmp(allocator, expected, cursor_offset) != 0
        || memcmp(allocator + following, expected + following,
                  sizeof(context.allocator_before) - following) != 0
        || (include_cursor
            && micros_as_native_allocator()->allocation_cursor
                != context.allocator_before.allocation_cursor)
        || memcmp(micros_as_native_ownership(), &context.ownership_before,
                  sizeof(context.ownership_before)) != 0
        || memcmp(micros_kernel_object_runtime_authoritative_registry(), &context.objects_before,
                  sizeof(context.objects_before)) != 0
        || memcmp(micros_as_native_ipc_input(), &context.endpoints_before,
                  sizeof(context.endpoints_before)) != 0
        || memcmp(micros_as_native_vm_input(), &context.vm_before, sizeof(context.vm_before)) != 0
        || memcmp(tty_handoff_context(), &context.tty_before, sizeof(context.tty_before)) != 0
        || memcmp(&context.bootstrap, &context.bootstrap_before, sizeof(context.bootstrap)) != 0
        || memcmp(&context.scheduler, &context.scheduler_before, sizeof(context.scheduler)) != 0
        || (kernel != NULL) != context.kernel_ready_before
        || (kernel != NULL && memcmp(kernel, &context.kernel_before, sizeof(*kernel)) != 0)
        || memcmp(context.arena, context.kernel_page_before, sizeof(context.kernel_page_before)) != 0
        || micros_as_native_scratch_busy() != context.scratch_busy_before) {
        return false;
    }
    for (index = 0; index < 2; ++index) {
        if (memcmp(context.scratch_before[index], micros_as_native_scratch_bitmap(index != 0),
                   sizeof(context.scratch_before[index])) != 0) {
            return false;
        }
#if !defined(MICROS_SELECTED_ADDRESS_SPACE_BASELINE)
        if (memcmp(&context.selection_before[index], micros_as_native_scratch_selection(index != 0),
                   sizeof(context.selection_before[index])) != 0) {
            return false;
        }
#endif
    }
#if !defined(MICROS_SELECTED_ADDRESS_SPACE_BASELINE)
    if (context.scratch_mode_before != micros_as_native_scratch_mode()) {
        return false;
    }
#endif
    for (index = 0; index < context.table_count; ++index) {
        if (memcmp(&context.tables_before[index],
                   (const void *)(uintptr_t)context.table_addresses[index],
                   MICROS_SV39_PAGE_SIZE) != 0) {
            return false;
        }
    }
    return true;
}

static bool capture_cursor_rollback(void)
{
    struct micros_frame_allocator *allocator = micros_as_native_allocator();
    struct micros_frame_ownership *ownership = micros_as_native_ownership();
    struct micros_kernel_objects *objects;
    const struct micros_process *process;
    struct micros_frame_owner kernel_owner = {
        .kind = MICROS_FRAME_OWNER_KERNEL_RETAINED,
    };
    uint64_t physical_address;
    uint64_t output = UINT64_C(0xfeedfacefeedface);
    enum micros_user_address_space_error observed;
    bool other_state;

    REQUIRE(initialize(512));
    objects = micros_kernel_object_runtime_authoritative_registry();
    while (allocator->allocation_cursor < 128) {
        REQUIRE(micros_frame_ownership_runtime_allocate(
            kernel_owner, &physical_address
        ) == MICROS_FRAME_OWNERSHIP_OK);
    }
    allocator->allocation_cursor = 64;
    REQUIRE(micros_frame_ownership_validate(ownership) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(micros_user_address_space_validate(context.process) == MICROS_USER_ADDRESS_SPACE_OK);
    REQUIRE(micros_process_resolve(objects, context.process, &process) == MICROS_KERNEL_OBJECT_OK);
    context.table_count = 1;
    context.table_addresses[0] = process->address_space_root;
    save_state();
    micros_user_address_space_test_fail_after_allocations(1);
    observed = micros_user_address_space_allocate_page(
        context.process, MICROS_USER_VIRTUAL_BASE,
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE, &output
    );
    other_state = state_matches(false);
    printf("MEM04_CURSOR version=1 observed=%u before=%" PRIu64 " after=%" PRIu64
           " other_state_unchanged=%u output_unchanged=%u allocator_unchanged=%u\n",
           (unsigned)observed,
           context.allocator_before.allocation_cursor, allocator->allocation_cursor,
           (unsigned)other_state, (unsigned)(output == UINT64_C(0xfeedfacefeedface)),
           (unsigned)(memcmp(allocator, &context.allocator_before, sizeof(*allocator)) == 0));
    REQUIRE(observed == MICROS_USER_ADDRESS_SPACE_ERROR_ALLOCATION);
    REQUIRE(other_state);
    REQUIRE(output == UINT64_C(0xfeedfacefeedface));
    return true;
}

static bool selected_tables(uint64_t address)
{
    const struct micros_process *process;
    struct micros_sv39_decoded_pte entry;
    const struct micros_user_address_space_page_table *table;
    unsigned int level;
    uint16_t index;

    REQUIRE(micros_process_resolve(
        micros_kernel_object_runtime_registry(), context.process, &process
    ) == MICROS_KERNEL_OBJECT_OK);
    context.table_addresses[0] = process->address_space_root;
    context.table_count = 1;
    for (level = 2; level != 0; --level) {
        table = micros_user_address_space_table_at(context.table_addresses[2 - level]);
        REQUIRE(micros_sv39_vpn_index(address, level, &index) == MICROS_SV39_OK);
        REQUIRE(micros_sv39_decode_pte(table->entries[index], &entry) == MICROS_SV39_OK);
        if (entry.kind == MICROS_SV39_PTE_ABSENT) {
            return true;
        }
        REQUIRE(entry.kind == MICROS_SV39_PTE_TABLE);
        context.table_addresses[context.table_count++] = entry.physical_address;
    }
    return true;
}

static bool capture_scoped_resolution(bool handed_off)
{
    struct micros_frame_owner owner;
    struct micros_user_address_space_page_table *leaf;
    uint64_t mapped;
    uint64_t physical = UINT64_C(0xfeedfacefeedface);
    uint64_t absent_physical = physical;
    uint64_t before_satp;
    uint64_t after_satp;
    uint32_t permissions = UINT32_MAX;
    uint32_t absent_permissions = UINT32_MAX;
    size_t contiguous = SIZE_MAX;
    enum micros_user_address_space_error lookup;
    enum micros_user_address_space_error absent;
    enum micros_user_address_space_error diagnostic;
    enum micros_user_address_space_error activation;

    REQUIRE(initialize(512));
    REQUIRE(micros_user_address_space_allocate_page(
        context.process, MICROS_USER_VIRTUAL_BASE,
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE, &mapped
    ) == MICROS_USER_ADDRESS_SPACE_OK);
    if (handed_off) {
        REQUIRE(micros_frame_owner_make_process(
            MICROS_FRAME_OWNER_PROCESS_USER, context.process, &owner
        ) == MICROS_FRAME_OWNERSHIP_OK);
        REQUIRE(micros_frame_ownership_prepare_handoff(
            micros_as_native_ownership(), mapped, owner, MICROS_FRAME_HANDOFF_VM_WIRED
        ) == MICROS_FRAME_OWNERSHIP_OK);
        REQUIRE(micros_frame_ownership_complete_handoff(
            micros_as_native_ownership()
        ) == MICROS_FRAME_OWNERSHIP_OK);
    }
    REQUIRE(micros_user_address_space_validate(context.process) == MICROS_USER_ADDRESS_SPACE_OK);
    REQUIRE(selected_tables(MICROS_USER_VIRTUAL_BASE));
    leaf = micros_user_address_space_table_at(context.table_addresses[2]);
    REQUIRE(leaf->entries[1] == 0 && leaf->entries[2] == 0);
    leaf->entries[1] = (UINT64_C(1) << 63) | UINT64_C(1);
    save_state();
    before_satp = context.satp;
    diagnostic = micros_user_address_space_validate(context.process);
    lookup = micros_user_address_space_lookup(
        context.process, MICROS_USER_VIRTUAL_BASE, &physical, &permissions
    );
    absent = micros_user_address_space_translate(
        context.process, MICROS_USER_VIRTUAL_BASE + 2 * MICROS_SV39_PAGE_SIZE + 17,
        &absent_physical, &absent_permissions, &contiguous
    );
    activation = micros_user_address_space_activate(context.process);
    after_satp = context.satp;
    REQUIRE(micros_user_address_space_activate_kernel() == MICROS_USER_ADDRESS_SPACE_OK);
    printf("MEM04_SCOPED version=1 phase=%u diagnostic=%u lookup=%u physical=%" PRIu64
           " mapped=%" PRIu64 " permissions=%" PRIu32 " absent=%u absent_outputs_unchanged=%u"
           " activation=%u before_satp=%" PRIu64 " after_satp=%" PRIu64
           " expected_satp=%" PRIu64 " restored_satp=%" PRIu64 " state_unchanged=%u\n",
           handed_off ? 2U : 1U, (unsigned)diagnostic, (unsigned)lookup, physical,
           mapped, permissions, (unsigned)absent,
           (unsigned)(absent_physical == UINT64_C(0xfeedfacefeedface)
                      && absent_permissions == UINT32_MAX && contiguous == SIZE_MAX),
           (unsigned)activation, before_satp, after_satp,
           MICROS_RISCV_SATP_MODE_SV39 | (context.table_addresses[0] >> 12),
           context.satp, (unsigned)state_matches(true));
    REQUIRE(state_matches(true));
    REQUIRE(context.satp == before_satp);
    leaf->entries[1] = 0;
    REQUIRE(micros_user_address_space_validate(context.process) == MICROS_USER_ADDRESS_SPACE_OK);
    return true;
}

static uint64_t frame_address(uint64_t index)
{
    uint64_t group = index / (context.frames / context.ranges);
    uint64_t within = index % (context.frames / context.ranges);

    return (uint64_t)(uintptr_t)context.arena
        + (group * (context.frames / context.ranges + 1) + within) * MICROS_FRAME_SIZE;
}

static bool allocate_fixture_page(uint64_t address, uint64_t *physical)
{
    const struct micros_frame_allocator *allocator = micros_as_native_allocator();
    uint64_t free_index;
    size_t remaining;
    uint64_t expected = 0;

    REQUIRE(selected_tables(address));
    remaining = 4 - context.table_count;
    for (free_index = 0; free_index < context.frames; ++free_index) {
        if ((allocator->allocated_bitmap[free_index / 64]
                & (UINT64_C(1) << (free_index % 64))) == 0
            && --remaining == 0) {
            expected = frame_address(free_index);
            break;
        }
    }
    REQUIRE(expected != 0);
    REQUIRE(micros_user_address_space_allocate_page(
        context.process, address,
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE, physical
    ) == MICROS_USER_ADDRESS_SPACE_OK);
    REQUIRE(*physical == expected);
    return true;
}

static bool handoff_fixture(void)
{
    struct micros_frame_ownership *ownership = micros_as_native_ownership();
    uint64_t index;

    for (index = 0; index < context.frames; ++index) {
        if (ownership->owners[index].kind == MICROS_FRAME_OWNER_PROCESS_USER) {
            REQUIRE(micros_frame_ownership_prepare_handoff(
                ownership, frame_address(index), ownership->owners[index],
                MICROS_FRAME_HANDOFF_VM_WIRED
            ) == MICROS_FRAME_OWNERSHIP_OK);
        }
    }
    REQUIRE(micros_frame_ownership_complete_handoff(ownership) == MICROS_FRAME_OWNERSHIP_OK);
    REQUIRE(micros_user_address_space_validate(context.process) == MICROS_USER_ADDRESS_SPACE_OK);
    return true;
}

static bool begin_capture(void)
{
    ++context.capture;
#if defined(MICROS_TEST_WORK_COUNTS)
    uint64_t producer = TEST_PRODUCER + MICROS_AS_NATIVE_CHANNEL;

#if !defined(MICROS_SELECTED_ADDRESS_SPACE_BASELINE)
    producer += 256;
#endif
    REQUIRE(micros_test_work_begin(
        producer, context.capture, TEST_OWNER_MASK
    ) == MICROS_TEST_WORK_OK);
#endif
    return true;
}

#if defined(MICROS_TEST_WORK_COUNTS)
static bool finish_capture(void)
{
    REQUIRE(micros_test_work_finish(&context.observation) == MICROS_TEST_WORK_OK);
    return true;
}

static void report_work(void)
{
    size_t owner;
    size_t kind;

    printf("MEM04_WORK {\"version\":%" PRIu32 ",\"reserved0\":%" PRIu32
           ",\"producer_label\":%" PRIu64 ",\"capture_label\":%" PRIu64
           ",\"owner_mask\":%" PRIu32 ",\"reserved1\":%" PRIu32 ",\"counts\":[",
           context.observation.version, context.observation.reserved0,
           context.observation.producer_label, context.observation.capture_label,
           context.observation.owner_mask, context.observation.reserved1);
    for (owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        printf("%s[", owner ? "," : "");
        for (kind = 0; kind < MICROS_WORK_KIND_COUNT; ++kind) {
            printf("%s%" PRIu64, kind ? "," : "", context.observation.counts[owner][kind]);
        }
        printf("]");
    }
    puts("]}");
}
#endif

static bool model_allocated_page(uint64_t address)
{
    const uint64_t first = 128;
    const size_t count = 4 - context.table_count;
    const uint64_t user_index = first + count - 1;
    uint64_t next_table = first;
    size_t index;

#if !defined(MICROS_SELECTED_ADDRESS_SPACE_BASELINE)
    struct micros_process_frame_index *slot = &context.ownership_before.process_frames[context.process.slot];
    uint32_t previous_head = slot->head;

    for (index = 0; index < count; ++index) {
        uint64_t frame = first + index;

        context.ownership_before.owner_links[frame].previous = frame == user_index ? 0 : (uint32_t)(frame + 2);
        context.ownership_before.owner_links[frame].next = frame == first ? previous_head : (uint32_t)frame;
    }
    if (previous_head != 0) {
        context.ownership_before.owner_links[previous_head - 1].previous = (uint32_t)(first + 1);
    }
    slot->head = (uint32_t)(user_index + 1);
    slot->count += (uint32_t)count;
#endif
    for (index = 0; index < count; ++index) {
        uint64_t frame = first + index;
        enum micros_frame_owner_kind kind = frame == user_index
            ? MICROS_FRAME_OWNER_PROCESS_USER : MICROS_FRAME_OWNER_PROCESS_PAGE_TABLE;

        context.allocator_before.allocated_bitmap[frame / 64] |= UINT64_C(1) << (frame % 64);
        --context.allocator_before.free_frame_count;
        context.allocator_before.allocation_cursor = frame + 1;
        context.ownership_before.owners[frame] = (struct micros_frame_owner){
            .generation = context.process.generation, .slot = context.process.slot, .kind = (uint8_t)kind,
        };
        ++context.ownership_before.owner_counts[kind];
        ++context.ownership_before.owned_frame_count;
    }
    while (context.table_count < 3) {
        uint64_t physical = frame_address(next_table++);
        size_t parent = context.table_count - 1;
        uint16_t slot = (uint16_t)((address >> (12 + 9 * (3 - context.table_count))) & 511);

        REQUIRE(micros_sv39_make_table_pte(
            physical, &context.tables_before[parent].entries[slot]
        ) == MICROS_SV39_OK);
        memset(&context.tables_before[context.table_count], 0, sizeof(context.tables_before[0]));
        context.table_addresses[context.table_count++] = physical;
    }
    REQUIRE(micros_sv39_make_leaf_pte(
        frame_address(user_index),
        MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE | MICROS_SV39_PERMISSION_USER,
        &context.tables_before[2].entries[(address >> 12) & 511]
    ) == MICROS_SV39_OK);
    for (index = 0; index < MICROS_FRAME_SIZE; ++index) {
        if (((const unsigned char *)(uintptr_t)frame_address(user_index))[index] != 0) {
            return false;
        }
    }
    return state_matches(true);
}

static enum micros_user_address_space_error invoke_query(
    enum query_operation operation,
    uint64_t address,
    uint64_t *physical,
    uint32_t *permissions,
    size_t *contiguous
)
{
    switch (operation) {
    case QUERY_LOOKUP:
        return micros_user_address_space_lookup(
            context.process, address, context.null_output ? NULL : physical, permissions
        );
    case QUERY_TRANSLATE:
        return micros_user_address_space_translate(
            context.process, address, physical, context.null_output ? NULL : permissions, contiguous
        );
    case QUERY_ACTIVATE:
        return micros_user_address_space_activate(context.process);
    case QUERY_DIAGNOSE:
        return micros_user_address_space_validate(context.process);
    case QUERY_ALLOCATE:
        return micros_user_address_space_allocate_page(
            context.process, address, context.allocation_permissions, context.null_output ? NULL : physical
        );
    case QUERY_SCHEDULER: {
        enum micros_scheduler_error result = scheduler_runtime_validate_selected_thread(
            &context.scheduler, micros_kernel_object_runtime_authoritative_registry(), context.thread
        );

        *physical = context.scheduler.selected_root;
        return (enum micros_user_address_space_error)result;
    }
#if !defined(MICROS_SELECTED_ADDRESS_SPACE_BASELINE)
    case QUERY_PREPARE:
        return micros_user_address_space_prepare_activation(
            context.process, context.null_output ? NULL : physical
        );
#endif
    default:
        return MICROS_USER_ADDRESS_SPACE_ERROR_ARGUMENT;
    }
}

static bool capture_prepared_query(enum query_operation operation, uint64_t address, uint64_t expected)
{
    uint64_t physical = TEST_SENTINEL;
    uint32_t permissions = UINT32_MAX;
    size_t contiguous = SIZE_MAX;
    uintptr_t saved_status = operation == QUERY_SCHEDULER || operation == QUERY_PREPARE
        || context.scenario == 147 ? UINT64_C(0x40000) : UINT64_C(0x40002);
    uint64_t before_satp;
    uint64_t after_satp;
    uint64_t expected_satp;
    uint64_t writes_before;
    uint64_t writes_after;
    bool unchanged;
    bool matches;
    enum micros_user_address_space_error observed;

    if (context.scenario == 122) {
        saved_status |= MICROS_RISCV_SSTATUS_SIE;
    }
    if (operation == QUERY_SCHEDULER) {
        context.scheduler.selected_root = TEST_SENTINEL;
        context.scheduler_before = context.scheduler;
    }
    context.status = saved_status;
    before_satp = context.satp;
    writes_before = context.satp_writes;
    expected_satp = MICROS_RISCV_SATP_MODE_SV39 | (context.table_addresses[0] >> 12);
    REQUIRE(begin_capture());
    observed = invoke_query(operation, address, &physical, &permissions, &contiguous);
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(finish_capture());
#endif
    after_satp = context.satp;
    writes_after = context.satp_writes;
    unchanged = state_matches(true);
    if (operation == QUERY_SCHEDULER && observed == MICROS_USER_ADDRESS_SPACE_OK) {
        context.scheduler_before.selected_root = expected;
    }
    matches = operation == QUERY_ALLOCATE && observed == MICROS_USER_ADDRESS_SPACE_OK
        ? model_allocated_page(address) : state_matches(true);
    printf("MEM04_QUERY case=%" PRIu64 " scenario=%u operation=%u phase=%u frames=%" PRIu64
           " ranges=%zu density=%zu address=%" PRIu64 " error=%u result_matches=%u"
           " permissions=%" PRIu32 " contiguous=%zu outputs_unchanged=%u state_unchanged=%u"
           " state_matches=%u irq_unchanged=%u root_unchanged=%u root_matches=%u"
           " allocation_limit=%zu active=%u satp_writes=%" PRIu64 "\n",
           context.capture, context.scenario, (unsigned)operation, (unsigned)micros_as_native_ownership()->phase,
           context.frames, context.ranges, context.density, address, (unsigned)observed,
           (unsigned)(physical == expected), permissions, contiguous,
           (unsigned)(physical == TEST_SENTINEL && permissions == UINT32_MAX && contiguous == SIZE_MAX),
           (unsigned)unchanged, (unsigned)matches, (unsigned)(context.status == saved_status),
           (unsigned)(after_satp == before_satp), (unsigned)(after_satp == expected_satp),
           context.allocation_limit, (unsigned)context.active, writes_after - writes_before);
#if defined(MICROS_TEST_WORK_COUNTS)
    report_work();
#endif
    if (context.active || micros_kernel_address_space_report() == NULL) {
        REQUIRE(context.satp == before_satp);
    } else {
        REQUIRE(micros_user_address_space_activate_kernel() == MICROS_USER_ADDRESS_SPACE_OK);
        REQUIRE(context.satp == (MICROS_RISCV_SATP_MODE_SV39
            | (micros_kernel_address_space_report()->root_physical_address >> 12)));
    }
    return true;
}

static bool capture_query(enum query_operation operation, uint64_t address, uint64_t expected)
{
    REQUIRE(selected_tables(address));
    save_state();
    return capture_prepared_query(operation, address, expected);
}

static bool capture_absent_paths(void)
{
    unsigned int phase;
    unsigned int depth;

    for (phase = 1; phase <= 2; ++phase) {
        for (depth = 1; depth <= 3; ++depth) {
            uint64_t address = MICROS_USER_VIRTUAL_BASE;
            uint64_t physical;

            REQUIRE(initialize(512));
            context.scenario = depth;
            if (depth != 1) {
                REQUIRE(allocate_fixture_page(MICROS_USER_VIRTUAL_BASE, &physical));
                address += depth == 2 ? UINT64_C(0x200000) : MICROS_SV39_PAGE_SIZE;
            }
            if (phase == 2) {
                REQUIRE(handoff_fixture());
            }
            REQUIRE(capture_query(QUERY_LOOKUP, address, 0));
            REQUIRE(capture_query(QUERY_TRANSLATE, address + 17, 0));
        }
    }
    return true;
}

static bool capture_poisoned_tables(void)
{
    unsigned int phase;
    unsigned int depth;

    for (phase = 1; phase <= 2; ++phase) {
        for (depth = 0; depth < 3; ++depth) {
            struct micros_kernel_objects *objects;
            uint64_t *entry = NULL;
            uint64_t physical;
            uint64_t original;
            uint64_t poison;

            REQUIRE(initialize(512));
            context.scenario = 10 + depth;
            REQUIRE(allocate_fixture_page(MICROS_USER_VIRTUAL_BASE, &physical));
            if (phase == 2) {
                REQUIRE(handoff_fixture());
            }
            REQUIRE(selected_tables(MICROS_USER_VIRTUAL_BASE));
            objects = micros_kernel_object_runtime_authoritative_registry();
            if (depth == 0) {
                original = objects->processes[context.process.slot].address_space_root;
                objects->processes[context.process.slot].address_space_root = MICROS_FRAME_SIZE;
            } else {
                struct micros_user_address_space_page_table *table =
                    micros_user_address_space_table_at(context.table_addresses[depth - 1]);

                entry = &table->entries[depth == 1 ? 1 : 0];
                original = *entry;
                REQUIRE(micros_sv39_make_table_pte(MICROS_FRAME_SIZE, &poison) == MICROS_SV39_OK);
                *entry = poison;
            }
            save_state();
            REQUIRE(capture_prepared_query(QUERY_LOOKUP, MICROS_USER_VIRTUAL_BASE, 0));
            REQUIRE(capture_prepared_query(QUERY_ACTIVATE, MICROS_USER_VIRTUAL_BASE, TEST_SENTINEL));
            if (entry == NULL) {
                objects->processes[context.process.slot].address_space_root = original;
            } else {
                *entry = original;
            }
            REQUIRE(micros_user_address_space_validate(context.process) == MICROS_USER_ADDRESS_SPACE_OK);
        }
    }
    return true;
}

static bool prepare_fixture_thread(bool assigned)
{
    struct micros_kernel_objects *objects = micros_kernel_object_runtime_authoritative_registry();
    struct micros_user_context user = {0};

    REQUIRE(micros_thread_create(objects, context.process, &context.thread) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_thread_attach_execution_context(
        objects, context.thread, (uintptr_t)context.thread_stack,
        (uintptr_t)(context.thread_stack + sizeof(context.thread_stack)), &user
    ) == MICROS_KERNEL_OBJECT_OK);
    if (assigned) {
        REQUIRE(micros_thread_scheduler_admit(
            objects, micros_kernel_object_runtime_boot_hart_handle(), context.thread,
            8, UINT64_C(1000000), true
        ) == MICROS_KERNEL_OBJECT_OK);
    }
    REQUIRE(micros_kernel_objects_validate(objects) == MICROS_KERNEL_OBJECT_OK);
    return true;
}

static bool selected_slot(uint64_t address, uint64_t **slot)
{
    struct micros_user_address_space_page_table *leaf;
    uint16_t index;

    REQUIRE(selected_tables(address));
    REQUIRE(context.table_count == 3);
    REQUIRE(micros_sv39_vpn_index(address, 0, &index) == MICROS_SV39_OK);
    leaf = micros_user_address_space_table_at(context.table_addresses[2]);
    *slot = &leaf->entries[index];
    return true;
}

static bool prepare_vm_fixture(unsigned int phase, uint64_t *low_physical, uint64_t **scratch_slot)
{
    struct micros_vm_handoff_state *state = micros_as_native_vm_input();
    struct micros_kernel_objects *objects;
    struct micros_process_handle vm_process;
    struct micros_process_handle child;
    struct micros_vm_boot_summary summary = {
        .version = MICROS_VM_BOOT_INFO_VERSION,
        .managed_range_count = 1, .address_space_count = 2, .mapping_count = 3,
        .managed_frame_count = 512, .vm_self_wired_frame_count = 2,
    };
    struct micros_frame_owner retained = {.kind = MICROS_FRAME_OWNER_KERNEL_RETAINED};
    micros_endpoint_t endpoint;
    uint64_t ignored;
    uint64_t transferable;

    REQUIRE(initialize(512));
    REQUIRE(allocate_fixture_page(MICROS_USER_VIRTUAL_BASE, low_physical));
    REQUIRE(allocate_fixture_page(MICROS_VM_SCRATCH_VIRTUAL_ADDRESS - MICROS_FRAME_SIZE, &ignored));
    objects = micros_kernel_object_runtime_authoritative_registry();
    vm_process = context.process;
    REQUIRE(micros_process_create_at(objects, 1, &context.foreign_process) == MICROS_KERNEL_OBJECT_OK);
    context.process = context.foreign_process;
    REQUIRE(micros_user_address_space_create(context.process) == MICROS_USER_ADDRESS_SPACE_OK);
    REQUIRE(allocate_fixture_page(MICROS_VM_SCRATCH_VIRTUAL_ADDRESS, &context.foreign_physical));
    context.process = vm_process;
    REQUIRE(selected_slot(MICROS_VM_SCRATCH_VIRTUAL_ADDRESS, scratch_slot));
    REQUIRE(prepare_fixture_thread(true));
    REQUIRE(micros_endpoint_pack(context.process, &endpoint) == MICROS_ENDPOINT_OK);
    summary.free_frame_count = micros_as_native_allocator()->free_frame_count;
    REQUIRE(micros_vm_handoff_state_prepare(
        state, 2, endpoint, 1, MICROS_PRIVILEGE_PROFILE_VM, context.process, context.thread,
        context.table_addresses[0], MICROS_USER_VIRTUAL_BASE, MICROS_VM_BOOT_INFO_SIZE, &summary
    ) == MICROS_VM_HANDOFF_OK);
    if (phase != 0) {
        REQUIRE(micros_frame_ownership_runtime_allocate(retained, &transferable) == MICROS_FRAME_OWNERSHIP_OK);
        REQUIRE(micros_frame_ownership_prepare_handoff(
            micros_as_native_ownership(), transferable, retained, MICROS_FRAME_HANDOFF_VM_TRANSFERABLE
        ) == MICROS_FRAME_OWNERSHIP_OK);
        REQUIRE(handoff_fixture());
        micros_vm_handoff_commit_prevalidated(state);
    }
    if (phase >= 2) {
        objects = micros_kernel_object_runtime_authoritative_registry();
        REQUIRE(micros_process_create_at(objects, 2, &child) == MICROS_KERNEL_OBJECT_OK);
        state->last_load_token = 1;
        state->load = (struct micros_vm_load_transaction){
            .phase = MICROS_VM_LOAD_PHASE_LOADING, .process_reservation_token = 1,
            .load_token = 1, .process = child, .scratch_phase = MICROS_VM_SCRATCH_CLEAR,
        };
        state->mappings[child.slot] = (struct micros_vm_kernel_mapping_record){
            .load_token = 1, .process = child, .phase = MICROS_VM_KERNEL_MAPPING_LOADING,
        };
        if (phase == 3) {
            state->load.scratch_phase = MICROS_VM_SCRATCH_MAPPED;
            state->load.scratch = (struct micros_vm_kernel_mapping_entry){
                .virtual_address = MICROS_USER_VIRTUAL_BASE, .physical_address = transferable,
                .permissions = MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE
                    | MICROS_SV39_PERMISSION_USER,
            };
            REQUIRE(micros_sv39_make_leaf_pte(
                transferable,
                MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE | MICROS_SV39_PERMISSION_USER,
                *scratch_slot
            ) == MICROS_SV39_OK);
        }
    }
    REQUIRE(micros_vm_handoff_state_validate(state) == MICROS_VM_HANDOFF_OK);
    REQUIRE(micros_user_address_space_validate(context.process) == MICROS_USER_ADDRESS_SPACE_OK);
    return true;
}

static bool capture_vm_bindings(void)
{
    unsigned int scenario;

    for (scenario = 20; scenario <= 29; ++scenario) {
        struct micros_vm_handoff_state *state = micros_as_native_vm_input();
        uint64_t low_physical;
        uint64_t *scratch_slot;
        unsigned int phase = scenario <= 23 ? scenario - 20 : 3;

        REQUIRE(prepare_vm_fixture(phase, &low_physical, &scratch_slot));
        context.scenario = scenario;
        if (scenario == 24) {
            memset(&state->load, 0, sizeof(state->load));
            memset(state->mappings, 0, sizeof(state->mappings));
        } else if (scenario == 25) {
            *scratch_slot = 0;
        } else if (scenario == 26) {
            REQUIRE(micros_sv39_make_leaf_pte(
                low_physical,
                MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE | MICROS_SV39_PERMISSION_USER,
                scratch_slot
            ) == MICROS_SV39_OK);
        } else if (scenario == 27) {
            state->mappings[state->load.process.slot].entry_count = MICROS_APPLICATION_RESIDENT_PAGE_MAX;
        } else if (scenario == 28) {
            state->mappings[MICROS_PROCESS_CAPACITY - 1].entry_count = 1;
        } else if (scenario == 29) {
            state->last_mapping_generation = state->last_load_token + 1;
        }
        REQUIRE(capture_query(QUERY_LOOKUP, MICROS_USER_VIRTUAL_BASE, low_physical));
        REQUIRE(capture_query(QUERY_LOOKUP, MICROS_VM_SCRATCH_VIRTUAL_ADDRESS, 0));
        REQUIRE(capture_query(QUERY_TRANSLATE, MICROS_VM_SCRATCH_VIRTUAL_ADDRESS + 17, 0));
        REQUIRE(capture_query(QUERY_ACTIVATE, MICROS_USER_VIRTUAL_BASE, TEST_SENTINEL));
        REQUIRE(capture_query(QUERY_DIAGNOSE, MICROS_USER_VIRTUAL_BASE, TEST_SENTINEL));
    }
    return true;
}

static bool prepare_tty_bootstrap(micros_endpoint_t endpoint)
{
    struct micros_tty_handoff_runtime_state *state = &tty_handoff_context()->tty_handoff_state;
    struct micros_bootstrap_binding *binding = &context.bootstrap.bindings[0];
    struct micros_bootstrap_manifest_entry *entry = &context.bootstrap.manifest.entries[0];
    struct micros_bootstrap_runtime_entry *transition = &context.bootstrap.transitions.entries[0];

    state->service_id = MICROS_TTY_SERVICE_ID;
    state->endpoint = endpoint;
    state->image_id = 1;
    state->profile_id = MICROS_PRIVILEGE_PROFILE_TTY;
    state->process = context.process;
    state->thread = context.thread;
    state->root_physical_address = context.table_addresses[0];
    REQUIRE(micros_tty_handoff_initialize(&state->handoff) == MICROS_TTY_HANDOFF_OK);
    context.bootstrap.entry_count = 1;
    context.bootstrap.binding_slots[MICROS_TTY_SERVICE_ID] = 1;
    context.bootstrap.plan.console_service_id = MICROS_TTY_SERVICE_ID;
    context.bootstrap.transitions.entry_count = 1;
    context.bootstrap.transitions.service_slots[MICROS_TTY_SERVICE_ID] = 1;
    *binding = (struct micros_bootstrap_binding){
        .service_id = MICROS_TTY_SERVICE_ID, .process = context.process, .thread = context.thread,
        .root = state->root_physical_address, .endpoint = endpoint, .scheduler_priority = 8,
        .scheduler_preemptible = true, .scheduler_quantum_counter_ticks = UINT64_C(1000000),
    };
    *entry = (struct micros_bootstrap_manifest_entry){
        .service_id = MICROS_TTY_SERVICE_ID, .image_id = 1, .process_slot = context.process.slot,
        .profile_id = MICROS_PRIVILEGE_PROFILE_TTY, .role_flags = MICROS_BOOTSTRAP_ROLE_CONSOLE_OWNER,
        .device_base = MICROS_TTY_UART_PHYSICAL_BASE, .device_length = MICROS_TTY_UART_MAPPED_LENGTH,
        .irq_source = MICROS_TTY_UART_IRQ_SOURCE,
    };
    *transition = (struct micros_bootstrap_runtime_entry){
        .service_id = MICROS_TTY_SERVICE_ID, .state = MICROS_BOOTSTRAP_SERVICE_PREPARED,
        .endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_RESERVED,
    };
    tty_handoff_context()->tty_bootstrap_state = &context.bootstrap;
    return true;
}

static bool release_tty_fixture(micros_endpoint_t endpoint)
{
    struct micros_tty_handoff *handoff = &tty_handoff_context()->tty_handoff_state.handoff;
    struct micros_kernel_objects *objects = micros_kernel_object_runtime_authoritative_registry();
    struct micros_bootstrap_runtime_entry *transition = &context.bootstrap.transitions.entries[0];
    struct micros_endpoint_registry *registry = micros_as_native_ipc_input();

    REQUIRE(micros_endpoint_install_profile(
        registry, objects, context.process, MICROS_PRIVILEGE_PROFILE_TTY
    ) == MICROS_ENDPOINT_OK);
    REQUIRE(micros_endpoint_activate(registry, objects, endpoint) == MICROS_ENDPOINT_OK);
    REQUIRE(micros_thread_scheduler_admit(
        objects, micros_kernel_object_runtime_boot_hart_handle(), context.thread,
        8, UINT64_C(1000000), true
    ) == MICROS_KERNEL_OBJECT_OK);
    transition->state = MICROS_BOOTSTRAP_SERVICE_STARTING;
    transition->endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE;
    transition->profile_installed = true;
    transition->scheduler_assigned = true;
    transition->ready_deadline = handoff->deadline;
    REQUIRE(micros_tty_handoff_release(handoff) == MICROS_TTY_HANDOFF_OK);
    return true;
}

static bool prepare_tty_fixture(unsigned int phase, uint64_t *low_physical, uint64_t **device_slot)
{
    const struct micros_privilege_profile profile = {
        .id = MICROS_PRIVILEGE_PROFILE_TTY, .name = "tty",
        .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
        .kernel_operations = MICROS_KERNEL_OPERATION_TTY_CONTROL,
    };
    struct micros_kernel_objects *objects;
    struct micros_tty_handoff *handoff = &tty_handoff_context()->tty_handoff_state.handoff;
    micros_endpoint_t endpoint;
    uint64_t ignored;

    REQUIRE(initialize(512));
    context.foreign_process = context.process;
    REQUIRE(allocate_fixture_page(MICROS_TTY_UART_VIRTUAL_BASE, &context.foreign_physical));
    objects = micros_kernel_object_runtime_authoritative_registry();
    REQUIRE(micros_process_create_at(
        objects, MICROS_TTY_PROCESS_SLOT, &context.process
    ) == MICROS_KERNEL_OBJECT_OK);
    REQUIRE(micros_user_address_space_create(context.process) == MICROS_USER_ADDRESS_SPACE_OK);
    REQUIRE(allocate_fixture_page(MICROS_USER_VIRTUAL_BASE, low_physical));
    REQUIRE(allocate_fixture_page(MICROS_TTY_UART_VIRTUAL_BASE - MICROS_FRAME_SIZE, &ignored));
    REQUIRE(selected_slot(MICROS_TTY_UART_VIRTUAL_BASE, device_slot));
    REQUIRE(prepare_fixture_thread(false));
    REQUIRE(micros_endpoint_registry_initialize(
        micros_as_native_ipc_input(), &profile, 1
    ) == MICROS_ENDPOINT_OK);
    REQUIRE(micros_endpoint_reserve(
        micros_as_native_ipc_input(), objects, context.process, &endpoint
    ) == MICROS_ENDPOINT_OK);
    micros_as_native_ipc_publish();
    REQUIRE(prepare_tty_bootstrap(endpoint));
    if (phase != 0) {
        REQUIRE(handoff_fixture());
        REQUIRE(micros_tty_handoff_begin(handoff, 10, 100) == MICROS_TTY_HANDOFF_OK);
    }
    if (phase >= 2) {
        REQUIRE(micros_sv39_make_leaf_pte(
            MICROS_TTY_UART_PHYSICAL_BASE,
            MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE | MICROS_SV39_PERMISSION_USER,
            *device_slot
        ) == MICROS_SV39_OK);
        REQUIRE(micros_tty_handoff_mark_mapped(handoff) == MICROS_TTY_HANDOFF_OK);
    }
    if (phase >= 3) {
        REQUIRE(release_tty_fixture(endpoint));
    }
    if (phase == 4) {
        REQUIRE(micros_tty_handoff_commit(handoff) == MICROS_TTY_HANDOFF_OK);
    }
    REQUIRE(micros_tty_handoff_runtime_validate(
        &context.bootstrap, micros_as_native_ipc_input(), objects
    ) == MICROS_TTY_HANDOFF_OK);
    REQUIRE(micros_user_address_space_validate(context.process) == MICROS_USER_ADDRESS_SPACE_OK);
    return true;
}

static bool capture_tty_bindings(void)
{
    unsigned int scenario;

    for (scenario = 40; scenario <= 52; ++scenario) {
        struct micros_tty_handoff_runtime_state *state = &tty_handoff_context()->tty_handoff_state;
        struct micros_kernel_objects *objects;
        uint64_t low_physical;
        uint64_t *device_slot;
        unsigned int phase = scenario <= 44 ? scenario - 40 : scenario == 45 ? 1 : 2;

        REQUIRE(prepare_tty_fixture(phase, &low_physical, &device_slot));
        context.scenario = scenario;
        objects = micros_kernel_object_runtime_authoritative_registry();
        if (scenario == 45 || scenario == 47) {
            REQUIRE(micros_sv39_make_leaf_pte(
                scenario == 45 ? MICROS_TTY_UART_PHYSICAL_BASE : low_physical,
                MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE | MICROS_SV39_PERMISSION_USER,
                device_slot
            ) == MICROS_SV39_OK);
        } else if (scenario == 46) {
            *device_slot = 0;
        } else if (scenario == 48) {
            objects->processes[context.process.slot].privilege_profile = MICROS_PRIVILEGE_PROFILE_TTY;
        } else if (scenario == 49) {
            objects->threads[context.thread.slot].context_attached = false;
        } else if (scenario == 50) {
            struct micros_process *unrelated = &objects->processes[MICROS_PROCESS_CAPACITY - 1];

            REQUIRE(unrelated->slot_state == MICROS_KERNEL_OBJECT_SLOT_FREE);
            REQUIRE(unrelated->live_thread_count == 0);
            unrelated->live_thread_count = 1;
            REQUIRE(micros_tty_handoff_runtime_validate(
                &context.bootstrap, micros_as_native_ipc_input(), objects
            ) == MICROS_TTY_HANDOFF_ERROR_INVARIANT);
            unrelated->live_thread_count = 0;
            micros_as_native_ipc_input()->endpoints[MICROS_PROCESS_CAPACITY - 1].value = 1;
        } else if (scenario == 51) {
            ++state->process.generation;
            REQUIRE(micros_endpoint_pack(state->process, &state->endpoint) == MICROS_ENDPOINT_OK);
        } else if (scenario == 52) {
            context.process = context.foreign_process;
            REQUIRE(capture_query(QUERY_LOOKUP, MICROS_TTY_UART_VIRTUAL_BASE, context.foreign_physical));
            REQUIRE(capture_query(
                QUERY_TRANSLATE, MICROS_TTY_UART_VIRTUAL_BASE + 17, context.foreign_physical + 17
            ));
            REQUIRE(capture_query(QUERY_ACTIVATE, MICROS_TTY_UART_VIRTUAL_BASE, TEST_SENTINEL));
            REQUIRE(capture_query(QUERY_DIAGNOSE, MICROS_TTY_UART_VIRTUAL_BASE, TEST_SENTINEL));
            continue;
        }
        REQUIRE(capture_query(QUERY_LOOKUP, MICROS_USER_VIRTUAL_BASE, low_physical));
        REQUIRE(capture_query(QUERY_LOOKUP, MICROS_TTY_UART_VIRTUAL_BASE, 0));
        REQUIRE(capture_query(QUERY_TRANSLATE, MICROS_TTY_UART_VIRTUAL_BASE + 17, 0));
        REQUIRE(capture_query(QUERY_ACTIVATE, MICROS_USER_VIRTUAL_BASE, TEST_SENTINEL));
        REQUIRE(capture_query(QUERY_DIAGNOSE, MICROS_USER_VIRTUAL_BASE, TEST_SENTINEL));
    }
    return true;
}

static bool check_bootstrap_uart_query(
    struct micros_process_handle process,
    uint64_t root,
    bool mapped,
    enum micros_user_address_space_error expected
)
{
    const uintptr_t status = context.status;
    const uint64_t satp = context.satp;
    const uint64_t writes = context.satp_writes;
    enum micros_user_address_space_error observed;

    save_state();
    REQUIRE(begin_capture());
    observed = micros_user_address_space_inspect_bootstrap_tty_uart(process, root, mapped);
#if defined(MICROS_TEST_WORK_COUNTS)
    REQUIRE(finish_capture());
    for (size_t owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
        REQUIRE(context.observation.counts[owner][MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL] == 0);
    }
#if MICROS_AS_NATIVE_CHANNEL == 0 || MICROS_AS_NATIVE_CHANNEL == 1
    if (context.observation.counts[MICROS_WORK_OWNER_KERNEL_MEMORY][MICROS_WORK_KIND_PTE_VISIT] > 3) {
        fprintf(stderr, "BOOT02 UART point case=%u pte_reads=%" PRIu64 " limit=3\n",
                context.scenario,
                context.observation.counts[MICROS_WORK_OWNER_KERNEL_MEMORY][MICROS_WORK_KIND_PTE_VISIT]);
        return false;
    }
    if (expected == MICROS_USER_ADDRESS_SPACE_OK) {
        REQUIRE(context.observation.counts[MICROS_WORK_OWNER_KERNEL_MEMORY][MICROS_WORK_KIND_PTE_VISIT] == 3);
    }
#endif
#if MICROS_AS_NATIVE_CHANNEL == 1
    REQUIRE(context.observation.counts[MICROS_WORK_OWNER_KERNEL_MEMORY][MICROS_WORK_KIND_RECORD_VISIT] <= 4);
    REQUIRE(context.observation.counts[MICROS_WORK_OWNER_KERNEL_MEMORY][MICROS_WORK_KIND_COPY_UNIT] == 0);
#endif
#if MICROS_AS_NATIVE_CHANNEL == 2
    REQUIRE(context.observation.counts[MICROS_WORK_OWNER_KERNEL_MEMORY][MICROS_WORK_KIND_CLEAR_UNIT] == 0);
#endif
#endif
    if (observed != expected) {
        fprintf(stderr, "BOOT02 UART point case=%u result=%u expected=%u\n",
                context.scenario, (unsigned)observed, (unsigned)expected);
        return false;
    }
    REQUIRE(state_matches(true));
    REQUIRE(context.status == status && context.satp == satp && context.satp_writes == writes);
    return true;
}

static bool check_bootstrap_uart_points(void)
{
    for (unsigned int scenario = 0; scenario < 17; ++scenario) {
        struct micros_user_address_space_runtime_view runtime;
        struct micros_user_address_space_scratch_lease lease;
        struct micros_process_handle process;
        struct micros_kernel_objects *objects;
        enum micros_user_address_space_error expected = MICROS_USER_ADDRESS_SPACE_OK;
        unsigned int phase = scenario < 5 ? scenario
            : scenario == 10 || scenario == 13 || scenario == 16 ? 1 : 2;
        uint64_t physical;
        uint64_t root;
        uint64_t *slot;
        bool mapped = phase >= 2 && scenario != 15;

        REQUIRE(prepare_tty_fixture(phase, &physical, &slot));
        context.scenario = scenario;
        objects = micros_kernel_object_runtime_authoritative_registry();
        process = context.process;
        root = context.table_addresses[0];
        switch (scenario) {
        case 0:
            expected = MICROS_USER_ADDRESS_SPACE_ERROR_PHASE;
            break;
        case 5: {
            uint64_t frame = (root - (uint64_t)(uintptr_t)context.arena) / MICROS_FRAME_SIZE;

            ++micros_as_native_ownership()->owners[frame].generation;
            expected = MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            break;
        }
        case 6:
            REQUIRE(micros_sv39_make_leaf_pte(
                MICROS_TTY_UART_PHYSICAL_BASE,
                MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_EXECUTE | MICROS_SV39_PERMISSION_USER,
                slot
            ) == MICROS_SV39_OK);
            expected = MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            break;
        case 7:
            *slot = 0;
            expected = MICROS_USER_ADDRESS_SPACE_ERROR_OWNERSHIP;
            break;
        case 8:
            objects->processes[MICROS_PROCESS_CAPACITY - 1].live_thread_count = 1;
            REQUIRE(micros_tty_handoff_runtime_validate(
                &context.bootstrap, micros_as_native_ipc_input(), objects
            ) == MICROS_TTY_HANDOFF_ERROR_INVARIANT);
            REQUIRE(micros_tty_handoff_runtime_validate_bootstrap_binding(
                &context.bootstrap, micros_as_native_ipc_input(), objects
            ) == MICROS_TTY_HANDOFF_OK);
            break;
        case 9:
            REQUIRE(micros_user_address_space_runtime_require_read(&runtime) == MICROS_USER_ADDRESS_SPACE_OK);
            REQUIRE(micros_user_address_space_acquire_scratch(&runtime, &lease) == MICROS_USER_ADDRESS_SPACE_OK);
            expected = MICROS_USER_ADDRESS_SPACE_ERROR_BUSY;
            break;
        case 10:
            context.satp = MICROS_RISCV_SATP_MODE_SV39 | (root >> 12);
            expected = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
            break;
        case 11:
            root += MICROS_FRAME_SIZE;
            expected = MICROS_USER_ADDRESS_SPACE_ERROR_STATE;
            break;
        case 12:
            ++process.generation;
            expected = MICROS_USER_ADDRESS_SPACE_ERROR_STALE;
            break;
        case 13:
            *slot = 2;
            expected = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
            break;
        case 14: {
            uint64_t other = objects->processes[context.foreign_process.slot].address_space_root;
            uint64_t frame = (other - (uint64_t)(uintptr_t)context.arena) / MICROS_FRAME_SIZE;

            ++micros_as_native_ownership()->owners[frame].generation;
            REQUIRE(micros_frame_ownership_runtime_validate(objects) != MICROS_FRAME_OWNERSHIP_OK);
            break;
        }
        case 15:
            expected = MICROS_USER_ADDRESS_SPACE_ERROR_CONFLICT;
            break;
        case 16:
            context.satp = MICROS_RISCV_SATP_MODE_SV39 | (root >> 12);
            *slot = 2;
            expected = MICROS_USER_ADDRESS_SPACE_ERROR_PTE;
            break;
        default:
            break;
        }
        context.status = scenario % 2 == 0 ? 0 : MICROS_RISCV_SSTATUS_SIE;
        REQUIRE(check_bootstrap_uart_query(process, root, mapped, expected));
        context.status = 0;
        if (scenario == 9) {
            micros_user_address_space_release_scratch(&lease);
        }
    }
    return true;
}

static bool check_bootstrap_vm_role(void)
{
    for (unsigned int scenario = 0; scenario < 6; ++scenario) {
        struct micros_privilege_profile profile = {
            .id = MICROS_PRIVILEGE_PROFILE_VM, .name = "vm",
            .operations = MICROS_PRIVILEGE_OPERATION_RECEIVE,
            .kernel_operations = MICROS_KERNEL_OPERATION_VM_HANDOFF,
        };
        struct micros_vm_handoff_state *state = micros_as_native_vm_input();
        struct micros_bootstrap_binding *binding = &context.bootstrap.bindings[0];
        struct micros_endpoint_registry *registry = micros_as_native_ipc_input();
        struct micros_kernel_objects *objects;
        micros_endpoint_t endpoint;
        uint64_t physical;
        uint64_t *slot;
        bool ready;

        REQUIRE(prepare_vm_fixture(1, &physical, &slot));
        objects = micros_kernel_object_runtime_authoritative_registry();
        REQUIRE(micros_thread_runtime_flags_set(
            objects, context.thread, MICROS_THREAD_RTS_INACTIVE
        ) == MICROS_KERNEL_OBJECT_OK);
        REQUIRE(micros_thread_scheduler_remove(objects, context.thread) == MICROS_KERNEL_OBJECT_OK);
        if (scenario == 4) {
            profile.kernel_operations = 0;
        }
        REQUIRE(micros_endpoint_registry_initialize(registry, &profile, 1) == MICROS_ENDPOINT_OK);
        REQUIRE(micros_endpoint_reserve(registry, objects, context.process, &endpoint) == MICROS_ENDPOINT_OK);
        REQUIRE(micros_endpoint_install_profile(
            registry, objects, context.process, MICROS_PRIVILEGE_PROFILE_VM
        ) == MICROS_ENDPOINT_OK);
        REQUIRE(micros_endpoint_activate(registry, objects, endpoint) == MICROS_ENDPOINT_OK);
        REQUIRE(micros_thread_scheduler_admit(
            objects, micros_kernel_object_runtime_boot_hart_handle(), context.thread,
            8, UINT64_C(1000000), true
        ) == MICROS_KERNEL_OBJECT_OK);
        micros_as_native_ipc_publish();
        context.bootstrap.phase = MICROS_BOOTSTRAP_PHASE_RUNNING;
        context.bootstrap.entry_count = 1;
        context.bootstrap.manifest.header.entry_count = 1;
        context.bootstrap.plan.entry_count = 1;
        context.bootstrap.plan.controller_service_id = 1;
        context.bootstrap.plan.vm_service_id = state->service_id;
        context.bootstrap.binding_slots[state->service_id] = 1;
        *binding = (struct micros_bootstrap_binding){
            .service_id = state->service_id, .process = context.process, .thread = context.thread,
            .root = state->root_physical_address, .endpoint = endpoint, .prepared_page_count = 1,
            .scheduler_priority = 8, .scheduler_preemptible = true,
            .scheduler_quantum_counter_ticks = UINT64_C(1000000),
        };
        context.bootstrap.manifest.entries[0] = (struct micros_bootstrap_manifest_entry){
            .service_id = state->service_id, .image_id = state->image_id,
            .process_slot = context.process.slot, .profile_id = MICROS_PRIVILEGE_PROFILE_VM,
            .role_flags = MICROS_BOOTSTRAP_ROLE_VM,
        };
        context.bootstrap.transitions.phase = MICROS_BOOTSTRAP_PHASE_RUNNING;
        context.bootstrap.transitions.entry_count = 1;
        context.bootstrap.transitions.controller_service_id = 1;
        context.bootstrap.transitions.service_slots[state->service_id] = 1;
        context.bootstrap.transitions.entries[0] = (struct micros_bootstrap_runtime_entry){
            .service_id = state->service_id, .state = MICROS_BOOTSTRAP_SERVICE_READY,
            .endpoint_state = MICROS_BOOTSTRAP_ENDPOINT_ACTIVE,
            .profile_installed = true, .scheduler_assigned = true,
        };
        if (scenario == 1) {
            state->mappings[MICROS_PROCESS_CAPACITY - 1].entry_count = 1;
            REQUIRE(micros_vm_handoff_state_validate(state) == MICROS_VM_HANDOFF_ERROR_INVARIANT);
        } else if (scenario == 2) {
            uint64_t frame =
                (state->root_physical_address - (uint64_t)(uintptr_t)context.arena) / MICROS_FRAME_SIZE;

            ++micros_as_native_ownership()->owners[frame].generation;
        } else if (scenario == 3) {
            ++binding->thread.generation;
        } else if (scenario == 5) {
            ++context.bootstrap.manifest.entries[0].image_id;
        }
        save_state();
        REQUIRE(begin_capture());
        ready = micros_vm_handoff_runtime_role_ready(
            &context.bootstrap, registry, objects, state->service_id, state->process, state->endpoint
        );
#if defined(MICROS_TEST_WORK_COUNTS)
        REQUIRE(finish_capture());
        for (size_t owner = 0; owner < MICROS_WORK_OWNER_COUNT; ++owner) {
            REQUIRE(context.observation.counts[owner][MICROS_WORK_KIND_COMPLETE_VALIDATOR_CALL] == 0);
        }
#endif
        REQUIRE(ready == (scenario < 2));
        REQUIRE(state_matches(true));
    }
    return true;
}

static bool capture_allocation_prefixes(void)
{
    struct micros_frame_owner retained = {.kind = MICROS_FRAME_OWNER_KERNEL_RETAINED};
    unsigned int shape;
    size_t limit;

    for (shape = 0; shape < 3; ++shape) {
        for (limit = 0; limit <= 3 - shape; ++limit) {
            struct micros_frame_allocator *allocator = micros_as_native_allocator();
            uint64_t address = MICROS_USER_VIRTUAL_BASE;
            uint64_t ignored;
            size_t index;

            REQUIRE(initialize(512));
            context.scenario = 70 + shape;
            context.allocation_limit = limit;
            if (shape != 0) {
                REQUIRE(allocate_fixture_page(MICROS_USER_VIRTUAL_BASE, &ignored));
                address += shape == 1 ? UINT64_C(0x200000) : MICROS_FRAME_SIZE;
            }
            while (allocator->allocation_cursor < 128) {
                REQUIRE(micros_frame_ownership_runtime_allocate(retained, &ignored) == MICROS_FRAME_OWNERSHIP_OK);
            }
            allocator->allocation_cursor = 64;
            REQUIRE(micros_frame_ownership_validate(micros_as_native_ownership()) == MICROS_FRAME_OWNERSHIP_OK);
            REQUIRE(micros_user_address_space_validate(context.process) == MICROS_USER_ADDRESS_SPACE_OK);
            for (index = 0; index < 3 - shape; ++index) {
                memset((void *)(uintptr_t)frame_address(128 + index), 0xa5, MICROS_FRAME_SIZE);
            }
            micros_user_address_space_test_fail_after_allocations(limit);
            REQUIRE(capture_query(QUERY_ALLOCATE, address, frame_address(130 - shape)));
        }
    }
    return true;
}

static bool corrupt_selected_path(unsigned int scenario, uint64_t data)
{
    struct micros_user_address_space_page_table *root =
        micros_user_address_space_table_at(context.table_addresses[0]);
    struct micros_user_address_space_page_table *middle =
        micros_user_address_space_table_at(context.table_addresses[1]);
    struct micros_user_address_space_page_table *leaf =
        micros_user_address_space_table_at(context.table_addresses[2]);
    struct micros_frame_ownership *ownership = micros_as_native_ownership();
    struct micros_frame_allocator *allocator = micros_as_native_allocator();
    uint64_t data_index = (data - (uint64_t)(uintptr_t)context.arena) / MICROS_FRAME_SIZE;
    const uint32_t rw = MICROS_SV39_PERMISSION_READ | MICROS_SV39_PERMISSION_WRITE
        | MICROS_SV39_PERMISSION_USER;
    uint64_t pte;
    uint64_t frame;

    switch (scenario) {
    case 80: root->entries[1] |= UINT64_C(1) << 63; break;
    case 81: middle->entries[0] |= UINT64_C(1) << 63; break;
    case 82: leaf->entries[0] |= UINT64_C(1) << 63; break;
    case 83:
    case 84:
        REQUIRE(micros_sv39_make_leaf_pte(data, rw, &pte) == MICROS_SV39_OK);
        if (scenario == 83) {
            root->entries[1] = pte;
        } else {
            middle->entries[0] = pte;
        }
        break;
    case 85:
        REQUIRE(micros_sv39_make_table_pte(context.table_addresses[1], &leaf->entries[0]) == MICROS_SV39_OK);
        break;
    case 86: leaf->entries[0] &= ~UINT64_C(1); break;
    case 87: leaf->entries[0] |= UINT64_C(1) << 8; break;
    case 88: leaf->entries[0] |= UINT64_C(1) << 5; break;
    case 89: leaf->entries[0] &= ~(UINT64_C(1) << 6); break;
    case 90: leaf->entries[0] &= ~(UINT64_C(1) << 7); break;
    case 91: leaf->entries[0] &= ~(UINT64_C(1) << 1); break;
    case 92: leaf->entries[0] |= UINT64_C(1) << 3; break;
    case 93: leaf->entries[0] &= ~(UINT64_C(1) << 4); break;
    case 94:
    case 95:
    case 96:
        frame = context.table_addresses[scenario == 96 ? 1 : 0];
        REQUIRE(micros_sv39_make_table_pte(frame, &pte) == MICROS_SV39_OK);
        if (scenario == 94) {
            root->entries[1] = pte;
        } else {
            middle->entries[0] = pte;
        }
        break;
    case 97:
        REQUIRE(micros_sv39_make_leaf_pte(context.table_addresses[0], rw, &leaf->entries[0]) == MICROS_SV39_OK);
        break;
    case 98: ++ownership->owners[data_index].generation; break;
    case 99: allocator->allocated_bitmap[data_index / 64] &= ~(UINT64_C(1) << (data_index % 64)); break;
    case 100: leaf->entries[1] = leaf->entries[0]; break;
    case 101: leaf->entries[1] = (UINT64_C(1) << 63) | 1; break;
    case 102: root->entries[0] = 1; break;
    case 103:
    case 104:
    case 105:
        frame = (context.table_addresses[scenario - 103] - (uint64_t)(uintptr_t)context.arena) / MICROS_FRAME_SIZE;
        ++ownership->owners[frame].generation;
        break;
    case 106:
        frame = (context.table_addresses[0] - (uint64_t)(uintptr_t)context.arena) / MICROS_FRAME_SIZE;
        allocator->allocated_bitmap[frame / 64] &= ~(UINT64_C(1) << (frame % 64));
        break;
    case 107: ++ownership->owners[data_index].slot; break;
    case 108:
        REQUIRE(micros_sv39_make_table_pte(frame_address(0), &root->entries[1]) == MICROS_SV39_OK);
        break;
    case 109:
        REQUIRE(micros_sv39_make_leaf_pte(frame_address(500), rw, &leaf->entries[0]) == MICROS_SV39_OK);
        break;
    case 110:
        ownership->owners[0].kind = MICROS_FRAME_OWNER_KERNEL_RETAINED;
        --ownership->owner_counts[MICROS_FRAME_OWNER_KERNEL_PAGE_TABLE];
        ++ownership->owner_counts[MICROS_FRAME_OWNER_KERNEL_RETAINED];
        break;
    case 111: leaf->entries[0] &= ~(UINT64_C(1) << 2); break;
    default: return false;
    }
    return true;
}

static bool capture_path_corruption(void)
{
    unsigned int phase;
    unsigned int scenario;

    for (phase = 1; phase <= 2; ++phase) {
        for (scenario = 80; scenario <= 111; ++scenario) {
            uint64_t physical;

            REQUIRE(initialize(512));
            context.scenario = scenario;
            REQUIRE(allocate_fixture_page(MICROS_USER_VIRTUAL_BASE, &physical));
            if (phase == 2) {
                REQUIRE(handoff_fixture());
            }
            REQUIRE(selected_tables(MICROS_USER_VIRTUAL_BASE));
            REQUIRE(corrupt_selected_path(scenario, physical));
            save_state();
            REQUIRE(capture_prepared_query(QUERY_LOOKUP, MICROS_USER_VIRTUAL_BASE, physical));
            REQUIRE(capture_prepared_query(QUERY_ACTIVATE, MICROS_USER_VIRTUAL_BASE, TEST_SENTINEL));
            REQUIRE(capture_prepared_query(QUERY_DIAGNOSE, MICROS_USER_VIRTUAL_BASE, TEST_SENTINEL));
        }
    }
    return true;
}

static bool capture_scheduler_roots(void)
{
    unsigned int scenario;

    for (scenario = 130; scenario <= 139; ++scenario) {
        struct micros_kernel_objects *objects;
        uint64_t physical;
        uint64_t *slot;

        if (scenario == 136) {
            REQUIRE(prepare_vm_fixture(3, &physical, &slot));
        } else if (scenario == 137) {
            REQUIRE(prepare_tty_fixture(3, &physical, &slot));
        } else {
            REQUIRE(initialize(512));
            REQUIRE(allocate_fixture_page(MICROS_USER_VIRTUAL_BASE, &physical));
            REQUIRE(prepare_fixture_thread(scenario != 131));
        }
        REQUIRE(selected_tables(MICROS_USER_VIRTUAL_BASE));
        context.scenario = scenario;
        objects = micros_kernel_object_runtime_authoritative_registry();
        if (scenario == 132) {
            objects->threads[context.thread.slot].context_attached = false;
        } else if (scenario == 133) {
            ++context.thread.generation;
        } else if (scenario == 134) {
            micros_user_address_space_table_at(context.table_addresses[2])->entries[1] =
                (UINT64_C(1) << 63) | 1;
        } else if (scenario == 135) {
            objects->processes[context.process.slot].address_space_root = MICROS_FRAME_SIZE;
        } else if (scenario >= 138) {
            REQUIRE(micros_user_address_space_activate(context.process) == MICROS_USER_ADDRESS_SPACE_OK);
            context.active = true;
        }
        save_state();
        REQUIRE(capture_prepared_query(
            scenario == 139 ? QUERY_ACTIVATE : QUERY_SCHEDULER,
            MICROS_USER_VIRTUAL_BASE,
            scenario == 139 ? TEST_SENTINEL : context.table_addresses[0]
        ));
    }
    return true;
}

static bool prepare_lagging_cursor(uint64_t target)
{
    struct micros_frame_allocator *allocator = micros_as_native_allocator();
    struct micros_frame_owner retained = {.kind = MICROS_FRAME_OWNER_KERNEL_RETAINED};
    uint64_t physical;

    while (allocator->allocation_cursor < target) {
        REQUIRE(micros_frame_ownership_runtime_allocate(retained, &physical) == MICROS_FRAME_OWNERSHIP_OK);
    }
    if (target == 128) {
        allocator->allocation_cursor = 64;
    }
    REQUIRE(micros_frame_ownership_validate(micros_as_native_ownership()) == MICROS_FRAME_OWNERSHIP_OK);
    return true;
}

static bool capture_admission_guards(void)
{
    const enum query_operation operations[] = {
        QUERY_LOOKUP, QUERY_TRANSLATE, QUERY_ALLOCATE, QUERY_ACTIVATE,
    };
    unsigned int scenario;

    for (scenario = 140; scenario <= 155; ++scenario) {
        struct micros_kernel_objects *objects;
        struct micros_user_address_space_runtime_view runtime;
        struct micros_user_address_space_scratch_lease lease;
        uint64_t address = MICROS_USER_VIRTUAL_BASE;
        uint64_t physical;
        size_t operation;

        REQUIRE(initialize(512));
        REQUIRE(allocate_fixture_page(address, &physical));
        REQUIRE(selected_tables(address));
        context.scenario = scenario;
        objects = micros_kernel_object_runtime_authoritative_registry();
        if (scenario == 140) {
            context.null_output = true;
            micros_as_native_ownership()->phase = (enum micros_frame_ownership_phase)3;
        } else if (scenario == 141) {
            micros_as_native_kernel_reset();
        } else if (scenario == 142) {
            micros_as_native_ownership()->phase = (enum micros_frame_ownership_phase)3;
        } else if (scenario == 143) {
            ++context.process.generation;
        } else if (scenario == 144 || scenario == 148) {
            objects->processes[context.process.slot].address_space_root = 0;
            if (scenario == 144) {
                address = 0;
            } else {
                context.allocation_permissions = 0;
            }
        } else if (scenario == 145) {
            address = 0;
        } else if (scenario == 146) {
            ++address;
            ++physical;
        } else if (scenario == 147) {
            REQUIRE(micros_user_address_space_runtime_require_read(&runtime) == MICROS_USER_ADDRESS_SPACE_OK);
            REQUIRE(micros_user_address_space_acquire_scratch(&runtime, &lease) == MICROS_USER_ADDRESS_SPACE_OK);
        } else if (scenario == 149 || scenario == 151 || scenario == 152) {
            REQUIRE(micros_user_address_space_activate(context.process) == MICROS_USER_ADDRESS_SPACE_OK);
            context.active = true;
            if (scenario != 149) {
                micros_user_address_space_table_at(context.table_addresses[2])->entries[
                    scenario == 151 ? 0 : 1
                ] |= UINT64_C(1) << 63;
            }
        } else if (scenario == 153) {
            REQUIRE(prepare_lagging_cursor(128));
            micros_user_address_space_table_at(context.table_addresses[2])->entries[1] =
                (UINT64_C(1) << 63) | 1;
            address += 2 * MICROS_FRAME_SIZE;
            physical = frame_address(128);
            memset((void *)(uintptr_t)physical, 0xa5, MICROS_FRAME_SIZE);
        } else if (scenario == 154) {
            REQUIRE(prepare_lagging_cursor(context.frames));
            address += MICROS_FRAME_SIZE;
            physical = 0;
        } else if (scenario == 155) {
            REQUIRE(handoff_fixture());
            address += MICROS_FRAME_SIZE;
            physical = 0;
        }
        save_state();
        for (operation = 0; operation < 4; ++operation) {
            if (scenario >= 154 && operations[operation] != QUERY_ALLOCATE) {
                continue;
            }
            REQUIRE(capture_prepared_query(
                operations[operation], address,
                operations[operation] == QUERY_ACTIVATE ? TEST_SENTINEL : physical
            ));
        }
        if (scenario == 147) {
            context.status &= ~(uintptr_t)MICROS_RISCV_SSTATUS_SIE;
            micros_user_address_space_release_scratch(&lease);
        }
    }
    return true;
}

static bool capture_prepare_interface(void)
{
#if !defined(MICROS_SELECTED_ADDRESS_SPACE_BASELINE)
    unsigned int scenario;

    for (scenario = 120; scenario <= 125; ++scenario) {
        struct micros_user_address_space_runtime_view runtime;
        struct micros_user_address_space_scratch_lease lease;
        uint64_t physical;

        REQUIRE(initialize(512));
        REQUIRE(allocate_fixture_page(MICROS_USER_VIRTUAL_BASE, &physical));
        REQUIRE(selected_tables(MICROS_USER_VIRTUAL_BASE));
        context.scenario = scenario;
        if (scenario == 121) {
            context.null_output = true;
        } else if (scenario == 123) {
            ++context.process.generation;
        } else if (scenario == 124) {
            REQUIRE(micros_user_address_space_runtime_require_read(&runtime) == MICROS_USER_ADDRESS_SPACE_OK);
            REQUIRE(micros_user_address_space_acquire_scratch(&runtime, &lease) == MICROS_USER_ADDRESS_SPACE_OK);
        } else if (scenario == 125) {
            micros_kernel_object_runtime_authoritative_registry()
                ->processes[context.process.slot].address_space_root = 0;
        }
        save_state();
        REQUIRE(capture_prepared_query(QUERY_PREPARE, MICROS_USER_VIRTUAL_BASE, context.table_addresses[0]));
        if (scenario == 124) {
            micros_user_address_space_release_scratch(&lease);
        }
    }
#endif
    return true;
}

static bool capture_inactive_owned_slots(void)
{
    unsigned int controller;
    unsigned int phase;

    for (controller = 0; controller < 2; ++controller) {
        for (phase = 0; phase < 2; ++phase) {
            uint64_t physical;
            uint64_t *slot;
            uint64_t unused;

            if (controller == 0) {
                REQUIRE(prepare_vm_fixture(0, &unused, &slot));
            } else {
                REQUIRE(prepare_tty_fixture(0, &unused, &slot));
            }
            context.scenario = (controller == 0 ? 30 : 53) + phase;
            REQUIRE(allocate_fixture_page(MICROS_VM_SCRATCH_VIRTUAL_ADDRESS, &physical));
            if (phase != 0) {
                REQUIRE(handoff_fixture());
                if (controller == 0) {
                    micros_vm_handoff_commit_prevalidated(micros_as_native_vm_input());
                } else {
                    REQUIRE(micros_tty_handoff_begin(
                        &tty_handoff_context()->tty_handoff_state.handoff, 10, 100
                    ) == MICROS_TTY_HANDOFF_OK);
                }
            }
            REQUIRE(capture_query(QUERY_LOOKUP, MICROS_VM_SCRATCH_VIRTUAL_ADDRESS, physical));
            REQUIRE(capture_query(QUERY_TRANSLATE, MICROS_VM_SCRATCH_VIRTUAL_ADDRESS + 17, physical + 17));
            REQUIRE(capture_query(QUERY_ACTIVATE, MICROS_VM_SCRATCH_VIRTUAL_ADDRESS, TEST_SENTINEL));
            REQUIRE(capture_query(QUERY_DIAGNOSE, MICROS_VM_SCRATCH_VIRTUAL_ADDRESS, TEST_SENTINEL));
        }
    }
    return true;
}

static bool capture_foreign_vm_slot(void)
{
    uint64_t unused;
    uint64_t *slot;

    REQUIRE(prepare_vm_fixture(3, &unused, &slot));
    context.scenario = 32;
    context.process = context.foreign_process;
    REQUIRE(capture_query(QUERY_LOOKUP, MICROS_VM_SCRATCH_VIRTUAL_ADDRESS, context.foreign_physical));
    REQUIRE(capture_query(
        QUERY_TRANSLATE, MICROS_VM_SCRATCH_VIRTUAL_ADDRESS + 17, context.foreign_physical + 17
    ));
    REQUIRE(capture_query(QUERY_ACTIVATE, MICROS_VM_SCRATCH_VIRTUAL_ADDRESS, TEST_SENTINEL));
    REQUIRE(capture_query(QUERY_DIAGNOSE, MICROS_VM_SCRATCH_VIRTUAL_ADDRESS, TEST_SENTINEL));
    return true;
}

#if !defined(MICROS_SELECTED_ADDRESS_SPACE_BASELINE)
static bool vm_binding_matches(
    const struct micros_user_address_space_vm_scratch_authority *binding,
    unsigned int case_id
)
{
    const struct micros_vm_handoff_state *before = &context.vm_before;
    const unsigned char zero[7] = {0};
    bool initialized = case_id != 0;
    bool active = case_id == 4;

    return binding->active == active && binding->reserved1 == 0
        && memcmp(binding->reserved0, zero, sizeof(zero)) == 0
        && binding->load_token == (active ? before->load.load_token : 0)
        && binding->process.slot == (initialized ? before->process.slot : 0)
        && binding->process.generation == (initialized ? before->process.generation : 0)
        && binding->root_physical_address == (initialized ? before->root_physical_address : 0)
        && binding->virtual_address == (initialized ? MICROS_VM_SCRATCH_VIRTUAL_ADDRESS : 0)
        && binding->physical_address == (active ? before->load.scratch.physical_address : 0);
}

static bool capture_vm_interfaces(void)
{
    unsigned int case_id;

    for (case_id = 0; case_id < 16; ++case_id) {
        struct micros_vm_handoff_state *state = micros_as_native_vm_input();
        struct micros_user_address_space_vm_scratch_authority portable;
        struct micros_user_address_space_vm_scratch_authority runtime;
        struct micros_user_address_space_vm_scratch_authority sentinel;
        enum micros_vm_handoff_error portable_status;
        enum micros_vm_scratch_authority_status runtime_status;
        bool unchanged;
        bool matches;
        uint64_t physical;
        uint64_t before_satp;
        uint64_t *slot;

        if (case_id == 0) {
            REQUIRE(initialize(512));
            REQUIRE(selected_tables(MICROS_USER_VIRTUAL_BASE));
        } else {
            REQUIRE(prepare_vm_fixture(case_id <= 4 ? case_id - 1 : 3, &physical, &slot));
        }
        if (case_id == 5) {
            state->load.phase = MICROS_VM_LOAD_PHASE_FROZEN;
            state->load.scratch_phase = MICROS_VM_SCRATCH_CLEAR;
            memset(&state->load.scratch, 0, sizeof(state->load.scratch));
            *slot = 0;
        } else if (case_id == 6) {
            state->load.phase = MICROS_VM_LOAD_PHASE_FROZEN;
        } else if (case_id == 7) {
            state->phase = MICROS_VM_HANDOFF_PHASE_PREPARED;
        } else if (case_id == 8) {
            state->load.phase = MICROS_VM_LOAD_PHASE_IDLE;
        } else if (case_id >= 9 && case_id <= 14) {
            struct micros_vm_kernel_mapping_record *record = &state->mappings[state->load.process.slot];
            switch (case_id) {
            case 9: record->root_physical_address = MICROS_FRAME_SIZE; break;
            case 10: record->table_frames[0] = MICROS_FRAME_SIZE; break;
            case 11: record->entry = MICROS_USER_VIRTUAL_BASE; break;
            case 12: record->stack_pointer = MICROS_USER_VIRTUAL_END - 16; break;
            case 13: record->exit_token = 1; break;
            case 14: record->mapping_generation = 1; break;
            default: return false;
            }
        } else if (case_id == 15) {
            state->load.scratch.reserved = 1;
        }
        save_state();
        before_satp = context.satp;
        memset(&sentinel, 0xa5, sizeof(sentinel));
        portable = sentinel;
        runtime = sentinel;
        portable_status = micros_vm_handoff_inspect_scratch_binding(state, &portable);
        runtime_status = micros_vm_handoff_runtime_inspect_scratch_binding(&runtime);
        unchanged = memcmp(&portable, &sentinel, sizeof(portable)) == 0
            && memcmp(&runtime, &sentinel, sizeof(runtime)) == 0;
        matches = case_id < 6
            ? vm_binding_matches(&portable, case_id) && vm_binding_matches(&runtime, case_id) : unchanged;
        printf("MEM04_BINDING controller=0 case=%u primary=%u runtime=%u output_matches=%u"
               " output_unchanged=%u state_unchanged=%u\n",
               case_id, (unsigned)portable_status, (unsigned)runtime_status, (unsigned)matches,
               (unsigned)unchanged,
               (unsigned)(state_matches(true) && context.status == 0 && context.satp == before_satp));
    }
    return true;
}

static bool capture_tty_interfaces(void)
{
    unsigned int case_id;

    for (case_id = 0; case_id < 12; ++case_id) {
        struct micros_tty_handoff_runtime_state *state = &tty_handoff_context()->tty_handoff_state;
        struct micros_kernel_objects *objects;
        struct micros_tty_device_authority binding;
        struct micros_tty_device_authority sentinel;
        enum micros_tty_device_authority_status status;
        bool unchanged;
        bool matches;
        uint64_t physical;
        uint64_t before_satp;
        uint64_t *slot;

        if (case_id == 0) {
            REQUIRE(initialize(512));
            REQUIRE(selected_tables(MICROS_USER_VIRTUAL_BASE));
        } else {
            REQUIRE(prepare_tty_fixture(
                case_id <= 5 ? case_id - 1 : case_id == 9 ? 3 : 2, &physical, &slot
            ));
        }
        objects = micros_kernel_object_runtime_authoritative_registry();
        switch (case_id) {
        case 6: ++state->root_physical_address; break;
        case 7: ++state->thread.generation; break;
        case 8: objects->processes[context.process.slot].privilege_profile = MICROS_PRIVILEGE_PROFILE_TTY; break;
        case 9: objects->threads[context.thread.slot].scheduler_assigned = false; break;
        case 10: objects->threads[context.thread.slot].context_attached = false; break;
        case 11: state->handoff.route_phase = MICROS_TTY_ROUTE_IN_SERVICE; break;
        default: break;
        }
        save_state();
        before_satp = context.satp;
        memset(&sentinel, 0xa5, sizeof(sentinel));
        binding = sentinel;
        status = micros_tty_handoff_runtime_inspect_device_binding(objects, &binding);
        unchanged = memcmp(&binding, &sentinel, sizeof(binding)) == 0;
        matches = case_id < 6
            ? binding.process.slot == (case_id ? state->process.slot : 0)
                && binding.process.generation == (case_id ? state->process.generation : 0)
                && binding.root_physical_address == (case_id ? state->root_physical_address : 0)
                && binding.console_phase == (case_id ? state->handoff.console_phase : 0)
            : unchanged;
        printf("MEM04_BINDING controller=1 case=%u primary=%u runtime=%u output_matches=%u"
               " output_unchanged=%u state_unchanged=%u\n",
               case_id, (unsigned)status, (unsigned)status, (unsigned)matches,
               (unsigned)unchanged,
               (unsigned)(state_matches(true) && context.status == 0 && context.satp == before_satp));
    }
    return true;
}

static bool capture_binding_arguments(void)
{
    struct micros_user_address_space_vm_scratch_authority binding;
    struct micros_user_address_space_vm_scratch_authority sentinel;
    enum micros_vm_handoff_error portable;
    enum micros_vm_scratch_authority_status runtime;
    bool matches;

    REQUIRE(initialize(512));
    REQUIRE(selected_tables(MICROS_USER_VIRTUAL_BASE));
    save_state();
    memset(&sentinel, 0xa5, sizeof(sentinel));
    binding = sentinel;
    portable = micros_vm_handoff_inspect_scratch_binding(NULL, &binding);
    runtime = micros_vm_handoff_runtime_inspect_scratch_binding(NULL);
    matches = micros_vm_handoff_inspect_scratch_binding(micros_as_native_vm_input(), NULL)
            == MICROS_VM_HANDOFF_ERROR_ARGUMENT
        && micros_tty_handoff_runtime_inspect_device_binding(
            micros_kernel_object_runtime_registry(), NULL
        ) == MICROS_TTY_DEVICE_AUTHORITY_INVARIANT;
    printf("MEM04_BINDING controller=2 case=0 primary=%u runtime=%u output_matches=%u"
           " output_unchanged=%u state_unchanged=%u\n",
           (unsigned)portable, (unsigned)runtime, (unsigned)matches,
           (unsigned)(memcmp(&binding, &sentinel, sizeof(binding)) == 0),
           (unsigned)(state_matches(true) && context.status == 0));
    return true;
}

static bool capture_rebound_private_walk(void)
{
    struct micros_user_address_space_runtime_view view;
    struct micros_user_address_space_scratch_lease lease;
    struct micros_sv39_decoded_pte leaf;
    struct micros_sv39_decoded_pte sentinel;
    enum micros_user_address_space_error observed;
    uint64_t physical;
    uint64_t *slot;
    bool matches;

    REQUIRE(prepare_vm_fixture(3, &physical, &slot));
    REQUIRE(micros_user_address_space_runtime_require_read(&view) == MICROS_USER_ADDRESS_SPACE_OK);
    REQUIRE(micros_user_address_space_acquire_scratch(&view, &lease) == MICROS_USER_ADDRESS_SPACE_OK);
    REQUIRE(micros_user_address_space_validate_all_process_roots_locked(
        &view, micros_kernel_object_runtime_registry(), &lease
    ) == MICROS_USER_ADDRESS_SPACE_OK);
    REQUIRE(view.process_handle.slot == context.foreign_process.slot);
    REQUIRE(selected_tables(MICROS_USER_VIRTUAL_BASE));
    save_state();
    memset(&sentinel, 0xa5, sizeof(sentinel));
    leaf = sentinel;
    observed = micros_user_address_space_lookup_leaf_locked(
        &view, context.process, MICROS_USER_VIRTUAL_BASE, &leaf, NULL, NULL, NULL
    );
    matches = leaf.kind == MICROS_SV39_PTE_LEAF && leaf.physical_address == physical
        && view.process_handle.slot == context.process.slot
        && view.process_handle.generation == context.process.generation
        && view.process_root == context.table_addresses[0];
    printf("MEM04_BINDING controller=3 case=0 primary=%u runtime=%u output_matches=%u"
           " output_unchanged=%u state_unchanged=%u\n",
           (unsigned)observed, (unsigned)observed, (unsigned)matches,
           (unsigned)(memcmp(&leaf, &sentinel, sizeof(leaf)) == 0),
           (unsigned)(state_matches(true) && context.status == 0));
    micros_user_address_space_release_scratch(&lease);
    return true;
}
#endif

static bool capture_binding_interfaces(void)
{
#if !defined(MICROS_SELECTED_ADDRESS_SPACE_BASELINE)
    return capture_vm_interfaces() && capture_tty_interfaces()
        && capture_binding_arguments() && capture_rebound_private_walk();
#else
    return true;
#endif
}

static bool capture_query_matrix(void)
{
    const uint64_t frames[] = {512, 4096, 262144, 512};
    const size_t ranges[] = {1, 1, 1, 16};
    const size_t densities[] = {1, 32};
    const uint64_t high = MICROS_USER_VIRTUAL_END - MICROS_SV39_PAGE_SIZE;
    size_t geometry;
    size_t density;
    unsigned int phase;

    for (phase = 1; phase <= 2; ++phase) {
        for (geometry = 0; geometry < 4; ++geometry) {
            for (density = 0; density < 2; ++density) {
                uint64_t low_physical = 0;
                uint64_t high_physical;
                uint64_t physical;
                size_t page;

                REQUIRE(initialize_geometry(frames[geometry], ranges[geometry]));
                context.density = densities[density];
                for (page = 0; page < context.density; ++page) {
                    REQUIRE(allocate_fixture_page(
                        MICROS_USER_VIRTUAL_BASE + page * MICROS_SV39_PAGE_SIZE, &physical
                    ));
                    if (page == 0) {
                        low_physical = physical;
                    }
                }
                REQUIRE(allocate_fixture_page(high, &high_physical));
                if (phase == 2) {
                    REQUIRE(handoff_fixture());
                }
                REQUIRE(capture_query(QUERY_LOOKUP, MICROS_USER_VIRTUAL_BASE, low_physical));
                REQUIRE(capture_query(QUERY_TRANSLATE, MICROS_USER_VIRTUAL_BASE + 17, low_physical + 17));
                REQUIRE(capture_query(QUERY_TRANSLATE, high + 4095, high_physical + 4095));
                REQUIRE(capture_query(QUERY_LOOKUP, MICROS_USER_VIRTUAL_BASE + UINT64_C(0x200000), 0));
                REQUIRE(capture_query(QUERY_ACTIVATE, MICROS_USER_VIRTUAL_BASE, TEST_SENTINEL));
                REQUIRE(capture_query(QUERY_DIAGNOSE, MICROS_USER_VIRTUAL_BASE, TEST_SENTINEL));
            }
        }
    }
    return true;
}

#if defined(MICROS_SELECTED_ADDRESS_SPACE_LAUNCHER)
static int launch_native_products(int argc, char **argv)
{
    char *arguments[15];
    const char *launcher = getenv("__PYVENV_LAUNCHER__");
    size_t count = 0;
    int index;

    if (argc != 4) {
        return 2;
    }
    if (launcher != NULL && launcher[0] != '\0') {
        fputs("MEM04 native orchestration: __PYVENV_LAUNCHER__ is unsupported\n", stderr);
        return 2;
    }
    arguments[count++] = MICROS_SELECTED_ADDRESS_SPACE_PYTHON;
    arguments[count++] = "-E";
    arguments[count++] = "-S";
    arguments[count++] = "-B";
    arguments[count++] = MICROS_SELECTED_ADDRESS_SPACE_DRIVER;
    arguments[count++] = "--native";
    for (index = 1; index < argc; ++index) {
        arguments[count++] = "--executable";
        arguments[count++] = argv[index];
    }
    arguments[count++] = "--executable";
    arguments[count++] = argv[0];
    arguments[count] = NULL;
    if (chdir(MICROS_SELECTED_ADDRESS_SPACE_ROOT) == 0) {
        execv(MICROS_SELECTED_ADDRESS_SPACE_PYTHON, arguments);
    }
    perror("MEM04 native orchestration");
    return 2;
}
#endif

int main(int argc, char **argv)
{
    bool passed;

#if defined(MICROS_SELECTED_ADDRESS_SPACE_LAUNCHER)
    if (argc == 4) {
        return launch_native_products(argc, argv);
    }
#endif
    if (argc != 3) {
        return 2;
    }
#if defined(MICROS_SELECTED_ADDRESS_SPACE_BASELINE)
    if (strcmp(argv[1], "red") != 0) {
        return 2;
    }
#else
    if (strcmp(argv[1], "green") != 0) {
        return 2;
    }
#endif
#if defined(MICROS_TEST_WORK_COUNTS)
    static const char *const channels[] = {"aggregate", "path", "context"};

    if (strcmp(argv[2], channels[MICROS_AS_NATIVE_CHANNEL]) != 0) {
        return 2;
    }
#else
    if (strcmp(argv[2], "ordinary") != 0) {
        return 2;
    }
#endif
    context.arena = mmap(NULL, ARENA_BYTES, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (context.arena == MAP_FAILED) {
        fputs("MEM04 native fixture allocation failed\n", stderr);
        return 2;
    }
    passed = capture_cursor_rollback()
        && capture_scoped_resolution(false)
        && capture_scoped_resolution(true)
        && capture_query_matrix()
        && capture_absent_paths()
        && capture_poisoned_tables()
        && capture_vm_bindings()
        && capture_tty_bindings()
        && capture_allocation_prefixes()
        && capture_path_corruption()
        && capture_scheduler_roots()
        && capture_admission_guards()
        && capture_inactive_owned_slots()
        && capture_foreign_vm_slot()
        && capture_prepare_interface()
        && capture_binding_interfaces()
        && check_bootstrap_uart_points()
        && check_bootstrap_vm_role();
    if (munmap(context.arena, ARENA_BYTES) != 0) {
        return 2;
    }
    return passed ? 0 : 1;
}
