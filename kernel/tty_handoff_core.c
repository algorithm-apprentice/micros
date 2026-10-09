#include "kernel/tty_handoff_core.h"

#include <stddef.h>

#include "micros/sv39.h"

#define MICROS_TTY_HANDOFF_INITIALIZATION_MAGIC \
    UINT64_C(0x4d49435254545948)

static void zero_bytes(void *value, size_t size)
{
    uint8_t *bytes = value;
    size_t index;

    for (index = 0; index < size; ++index) {
        bytes[index] = 0;
    }
}

static bool console_phase_is_preowned(
    enum micros_tty_console_phase phase
)
{
    return (
        phase == MICROS_TTY_CONSOLE_MAP_REQUESTED
        || phase == MICROS_TTY_CONSOLE_MAPPED
        || phase == MICROS_TTY_CONSOLE_STARTING
    );
}

static bool console_phase_has_device_mapping(
    enum micros_tty_console_phase phase
)
{
    return (
        phase == MICROS_TTY_CONSOLE_MAPPED
        || phase == MICROS_TTY_CONSOLE_STARTING
        || phase == MICROS_TTY_CONSOLE_OWNED
    );
}

static bool process_is_valid(struct micros_process_handle process)
{
    return (
        process.slot < MICROS_PROCESS_CAPACITY
        && process.generation != 0
        && process.generation <= MICROS_PROCESS_GENERATION_MAX
    );
}

static bool processes_equal(
    struct micros_process_handle left,
    struct micros_process_handle right
)
{
    return (
        left.slot == right.slot
        && left.generation == right.generation
    );
}

static bool device_authority_is_valid(
    const struct micros_tty_device_authority *authority
)
{
    return (
        authority != NULL
        && process_is_valid(authority->process)
        && authority->root_physical_address != 0
        && authority->root_physical_address
            % MICROS_SV39_PAGE_SIZE == 0
        && console_phase_has_device_mapping(
            authority->console_phase
        )
    );
}

enum micros_tty_handoff_error micros_tty_handoff_validate(
    const struct micros_tty_handoff *state
)
{
    if (state == NULL) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    if (
        state->initialization_magic
            != MICROS_TTY_HANDOFF_INITIALIZATION_MAGIC
    ) {
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }

    if (state->console_phase == MICROS_TTY_CONSOLE_EARLY) {
        if (
            state->route_phase != MICROS_TTY_ROUTE_DISABLED
            || state->deadline_armed
            || state->deadline != 0
            || state->claimed_source != 0
        ) {
            return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
        }
        return MICROS_TTY_HANDOFF_OK;
    }
    if (console_phase_is_preowned(state->console_phase)) {
        if (
            state->route_phase != MICROS_TTY_ROUTE_DISABLED
            || !state->deadline_armed
            || state->deadline == 0
            || state->claimed_source != 0
        ) {
            return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
        }
        return MICROS_TTY_HANDOFF_OK;
    }
    if (state->console_phase == MICROS_TTY_CONSOLE_OWNED) {
        if (
            (
                state->route_phase != MICROS_TTY_ROUTE_IDLE
                && state->route_phase
                    != MICROS_TTY_ROUTE_IN_SERVICE
            )
            || (
                state->deadline_armed
                    ? state->deadline == 0
                    : state->deadline != 0
            )
            || (
                state->route_phase == MICROS_TTY_ROUTE_IDLE
                    ? state->claimed_source != 0
                    : state->claimed_source
                        != MICROS_TTY_UART_IRQ_SOURCE
            )
        ) {
            return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
        }
        return MICROS_TTY_HANDOFF_OK;
    }
    if (state->console_phase == MICROS_TTY_CONSOLE_PANIC) {
        if (
            state->route_phase != MICROS_TTY_ROUTE_PANIC
            || state->deadline_armed
            || state->deadline != 0
            || state->claimed_source != 0
        ) {
            return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
        }
        return MICROS_TTY_HANDOFF_OK;
    }
    return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
}

enum micros_tty_handoff_error micros_tty_handoff_initialize(
    struct micros_tty_handoff *state
)
{
    if (state == NULL) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    zero_bytes(state, sizeof(*state));
    state->initialization_magic =
        MICROS_TTY_HANDOFF_INITIALIZATION_MAGIC;
    state->console_phase = MICROS_TTY_CONSOLE_EARLY;
    state->route_phase = MICROS_TTY_ROUTE_DISABLED;
    return MICROS_TTY_HANDOFF_OK;
}

enum micros_tty_handoff_error micros_tty_handoff_begin(
    struct micros_tty_handoff *state,
    uint64_t now,
    uint64_t interval
)
{
    enum micros_tty_handoff_error error;

    if (state == NULL || interval == 0) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_tty_handoff_validate(state);
    if (error != MICROS_TTY_HANDOFF_OK) {
        return error;
    }
    if (
        state->console_phase != MICROS_TTY_CONSOLE_EARLY
        || state->route_phase != MICROS_TTY_ROUTE_DISABLED
    ) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    if (UINT64_MAX - now < interval) {
        return MICROS_TTY_HANDOFF_ERROR_RANGE;
    }

    state->deadline = now + interval;
    state->deadline_armed = true;
    state->console_phase = MICROS_TTY_CONSOLE_MAP_REQUESTED;
    return MICROS_TTY_HANDOFF_OK;
}

enum micros_tty_handoff_error micros_tty_handoff_mark_mapped(
    struct micros_tty_handoff *state
)
{
    enum micros_tty_handoff_error error;

    if (state == NULL) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_tty_handoff_validate(state);
    if (error != MICROS_TTY_HANDOFF_OK) {
        return error;
    }
    if (
        state->console_phase
        != MICROS_TTY_CONSOLE_MAP_REQUESTED
    ) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    state->console_phase = MICROS_TTY_CONSOLE_MAPPED;
    return MICROS_TTY_HANDOFF_OK;
}

enum micros_tty_handoff_error micros_tty_handoff_release(
    struct micros_tty_handoff *state
)
{
    enum micros_tty_handoff_error error;

    if (state == NULL) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_tty_handoff_validate(state);
    if (error != MICROS_TTY_HANDOFF_OK) {
        return error;
    }
    if (state->console_phase != MICROS_TTY_CONSOLE_MAPPED) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    state->console_phase = MICROS_TTY_CONSOLE_STARTING;
    return MICROS_TTY_HANDOFF_OK;
}

enum micros_tty_handoff_error micros_tty_handoff_commit(
    struct micros_tty_handoff *state
)
{
    enum micros_tty_handoff_error error;

    if (state == NULL) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_tty_handoff_validate(state);
    if (error != MICROS_TTY_HANDOFF_OK) {
        return error;
    }
    if (
        state->console_phase != MICROS_TTY_CONSOLE_STARTING
        || state->route_phase != MICROS_TTY_ROUTE_DISABLED
    ) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    state->console_phase = MICROS_TTY_CONSOLE_OWNED;
    state->route_phase = MICROS_TTY_ROUTE_IDLE;
    return MICROS_TTY_HANDOFF_OK;
}

enum micros_tty_handoff_error micros_tty_handoff_claim(
    struct micros_tty_handoff *state,
    uint32_t source
)
{
    enum micros_tty_handoff_error error;

    if (state == NULL) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_tty_handoff_validate(state);
    if (error != MICROS_TTY_HANDOFF_OK) {
        return error;
    }
    if (source != MICROS_TTY_UART_IRQ_SOURCE) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    if (
        state->console_phase != MICROS_TTY_CONSOLE_OWNED
        || state->route_phase != MICROS_TTY_ROUTE_IDLE
    ) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    state->claimed_source = source;
    state->route_phase = MICROS_TTY_ROUTE_IN_SERVICE;
    return MICROS_TTY_HANDOFF_OK;
}

enum micros_tty_handoff_error micros_tty_handoff_complete(
    struct micros_tty_handoff *state,
    uint32_t source
)
{
    enum micros_tty_handoff_error error;

    if (state == NULL) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_tty_handoff_validate(state);
    if (error != MICROS_TTY_HANDOFF_OK) {
        return error;
    }
    if (
        state->console_phase != MICROS_TTY_CONSOLE_OWNED
        || state->route_phase
            != MICROS_TTY_ROUTE_IN_SERVICE
    ) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    if (
        source != MICROS_TTY_UART_IRQ_SOURCE
        || source != state->claimed_source
    ) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    state->claimed_source = 0;
    state->route_phase = MICROS_TTY_ROUTE_IDLE;
    return MICROS_TTY_HANDOFF_OK;
}

enum micros_tty_handoff_error micros_tty_handoff_accept_ready(
    struct micros_tty_handoff *state,
    uint64_t now
)
{
    enum micros_tty_handoff_error error;

    if (state == NULL) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    error = micros_tty_handoff_validate(state);
    if (error != MICROS_TTY_HANDOFF_OK) {
        return error;
    }
    if (
        state->console_phase != MICROS_TTY_CONSOLE_OWNED
        || !state->deadline_armed
        || now >= state->deadline
    ) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }
    state->deadline = 0;
    state->deadline_armed = false;
    return MICROS_TTY_HANDOFF_OK;
}

bool micros_tty_handoff_deadline_expired(
    const struct micros_tty_handoff *state,
    uint64_t now
)
{
    return (
        micros_tty_handoff_validate(state)
            == MICROS_TTY_HANDOFF_OK
        && state->deadline_armed
        && now >= state->deadline
    );
}

enum micros_tty_handoff_error micros_tty_handoff_panic(
    struct micros_tty_handoff *state
)
{
    if (state == NULL) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    if (
        state->initialization_magic
            != MICROS_TTY_HANDOFF_INITIALIZATION_MAGIC
    ) {
        return MICROS_TTY_HANDOFF_ERROR_INVARIANT;
    }
    state->console_phase = MICROS_TTY_CONSOLE_PANIC;
    state->route_phase = MICROS_TTY_ROUTE_PANIC;
    state->deadline = 0;
    state->claimed_source = 0;
    state->deadline_armed = false;
    return MICROS_TTY_HANDOFF_OK;
}

bool micros_tty_device_leaf_required(
    const struct micros_tty_device_authority *authority,
    struct micros_process_handle process,
    uint64_t root_physical_address
)
{
    return (
        device_authority_is_valid(authority)
        && processes_equal(authority->process, process)
        && authority->root_physical_address
            == root_physical_address
    );
}

bool micros_tty_device_range_intersects(
    const struct micros_tty_device_authority *authority,
    struct micros_process_handle process,
    uintptr_t base,
    size_t length
)
{
    const uintptr_t uart_base =
        (uintptr_t)MICROS_TTY_UART_VIRTUAL_BASE;
    const uintptr_t uart_end =
        uart_base + (uintptr_t)MICROS_TTY_UART_MAPPED_LENGTH;
    uintptr_t end;

    if (
        !device_authority_is_valid(authority)
        || !process_is_valid(process)
        || !processes_equal(authority->process, process)
        || length == 0
        || UINTPTR_MAX - base < length
    ) {
        return false;
    }
    end = base + length;
    return base < uart_end && end > uart_base;
}

enum micros_tty_handoff_error micros_tty_device_leaf_classify(
    const struct micros_tty_device_authority *authority,
    struct micros_process_handle process,
    uint64_t root_physical_address,
    uint64_t virtual_address,
    uint64_t physical_address,
    uint32_t permissions,
    enum micros_tty_device_leaf_class *classification
)
{
    const uint32_t exact_permissions =
        MICROS_SV39_PERMISSION_READ
        | MICROS_SV39_PERMISSION_WRITE
        | MICROS_SV39_PERMISSION_USER;
    enum micros_tty_device_leaf_class candidate =
        MICROS_TTY_DEVICE_LEAF_MANAGED;
    bool exact_owner;

    if (
        authority == NULL
        || classification == NULL
        || !process_is_valid(process)
        || root_physical_address == 0
        || root_physical_address % MICROS_SV39_PAGE_SIZE != 0
        || virtual_address % MICROS_SV39_PAGE_SIZE != 0
        || physical_address % MICROS_SV39_PAGE_SIZE != 0
    ) {
        return MICROS_TTY_HANDOFF_ERROR_ARGUMENT;
    }
    if (!device_authority_is_valid(authority)) {
        return MICROS_TTY_HANDOFF_ERROR_STATE;
    }

    exact_owner = (
        processes_equal(authority->process, process)
        && authority->root_physical_address
            == root_physical_address
    );
    if (physical_address == MICROS_TTY_UART_PHYSICAL_BASE) {
        candidate = (
            exact_owner
            && virtual_address == MICROS_TTY_UART_VIRTUAL_BASE
            && permissions == exact_permissions
        )
            ? MICROS_TTY_DEVICE_LEAF_EXACT
            : MICROS_TTY_DEVICE_LEAF_INVALID;
    } else if (
        exact_owner
        && virtual_address == MICROS_TTY_UART_VIRTUAL_BASE
    ) {
        candidate = MICROS_TTY_DEVICE_LEAF_INVALID;
    }
    *classification = candidate;
    return MICROS_TTY_HANDOFF_OK;
}
