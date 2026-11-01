#ifndef MICROS_STACK_OBSERVATION_H
#define MICROS_STACK_OBSERVATION_H

#ifdef MICROS_STACK_OBSERVER_KERNEL

#include <stdint.h>

#include "micros/kernel_objects.h"

struct micros_stack_observation {
    volatile uint64_t generation;
    volatile uint64_t use;
    volatile uint64_t samples;
    volatile uint64_t failure;
    volatile uint64_t closed;
    uint64_t reserved[3];
};

struct micros_stack_user_image {
    uint32_t image_id;
    const char *name;
    uint64_t address;
    uint64_t size;
    const char *elf_sha256;
    size_t region_count;
    struct {
        uint64_t address;
        uint64_t size;
        uint64_t digest;
    } regions[2];
};

_Static_assert(
    sizeof(struct micros_stack_observation) == 64,
    "stack observation ABI mismatch"
);

extern struct micros_stack_observation micros_stack_observations[131];
extern volatile uint64_t micros_stack_observer_failure;

void micros_stack_observer_attached(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
);
void micros_stack_observer_detaching(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
);
void micros_stack_observer_report(void);
void micros_stack_observer_write_record(
    const char *image,
    const char *owner,
    uint16_t slot,
    uint64_t generation,
    const struct micros_stack_observation *record,
    uint64_t bottom,
    uint64_t top
);
#ifdef MICROS_STACK_OBSERVER_USERS
void micros_stack_observer_capture_bootstrap_users(void);
#endif
#ifdef MICROS_STACK_OBSERVER_APPLICATIONS
void micros_stack_observer_user_attached(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
);
void micros_stack_observer_user_detaching(
    const struct micros_kernel_objects *objects,
    struct micros_thread_handle thread
);
#endif

#define MICROS_TEST_STACK_ATTACHED(objects, thread) \
    micros_stack_observer_attached((objects), (thread))
#define MICROS_TEST_STACK_DETACHING(objects, thread) \
    micros_stack_observer_detaching((objects), (thread))
#define MICROS_TEST_STACK_REPORT() micros_stack_observer_report()

#else

#define MICROS_TEST_STACK_ATTACHED(objects, thread) ((void)0)
#define MICROS_TEST_STACK_DETACHING(objects, thread) ((void)0)
#define MICROS_TEST_STACK_REPORT() ((void)0)

#endif

#endif
