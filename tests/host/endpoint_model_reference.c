#include "endpoint_model_internal.h"

#include <stdio.h>
#include <string.h>

bool endpoint_model_phase_has_endpoint(enum model_endpoint_phase phase)
{
    return (
        phase == MODEL_ENDPOINT_RESERVED
        || phase == MODEL_ENDPOINT_PROFILED
        || phase == MODEL_ENDPOINT_ACTIVE
    );
}

size_t endpoint_model_find_slot(
    const struct model_slot model[MODEL_SLOT_COUNT],
    uint32_t random,
    bool (*matches)(const struct model_slot *slot)
)
{
    size_t start = (random >> 16) % MODEL_SLOT_COUNT;
    size_t offset;

    for (offset = 0; offset < MODEL_SLOT_COUNT; ++offset) {
        size_t index = (start + offset) % MODEL_SLOT_COUNT;

        if (matches(&model[index])) {
            return index;
        }
    }
    return MODEL_SLOT_COUNT;
}

bool endpoint_model_slot_is_live(const struct model_slot *slot)
{
    return slot->process_live;
}

bool endpoint_model_slot_has_endpoint(const struct model_slot *slot)
{
    return slot->process_live
        && endpoint_model_phase_has_endpoint(slot->endpoint_phase);
}

bool endpoint_model_slot_has_nonactive_endpoint(const struct model_slot *slot)
{
    return slot->process_live
        && (
            slot->endpoint_phase == MODEL_ENDPOINT_RESERVED
            || slot->endpoint_phase == MODEL_ENDPOINT_PROFILED
        );
}

bool endpoint_model_slot_is_releasable(const struct model_slot *slot)
{
    return slot->process_live
        && !endpoint_model_phase_has_endpoint(slot->endpoint_phase);
}

bool endpoint_model_slot_has_stale_endpoint(const struct model_slot *slot)
{
    return slot->stale_endpoint != 0;
}

size_t endpoint_model_find_active_profile(
    const struct model_slot model[MODEL_SLOT_COUNT],
    uint32_t random,
    uint8_t profile
)
{
    size_t start = (random >> 16) % MODEL_SLOT_COUNT;
    size_t offset;

    for (offset = 0; offset < MODEL_SLOT_COUNT; ++offset) {
        size_t index = (start + offset) % MODEL_SLOT_COUNT;

        if (
            model[index].process_live
            && model[index].endpoint_phase == MODEL_ENDPOINT_ACTIVE
            && model[index].profile == profile
        ) {
            return index;
        }
    }
    return MODEL_SLOT_COUNT;
}

bool endpoint_model_target_is_allowed(
    uint8_t source_profile,
    uint32_t operation,
    uint8_t target_profile
)
{
    if (
        source_profile == 1
        && (
            (
                target_profile == 2
                && operation == MICROS_PRIVILEGE_OPERATION_CALL
            )
            || (
                target_profile == 1
                && operation == MICROS_PRIVILEGE_OPERATION_NOTIFY
            )
        )
    ) {
        return true;
    }
    return (
        source_profile == 2
        && (
            (
                target_profile == 1
                && operation == MICROS_PRIVILEGE_OPERATION_SEND
            )
            || (
                target_profile == 2
                && operation == MICROS_PRIVILEGE_OPERATION_NOTIFY
            )
        )
    );
}
