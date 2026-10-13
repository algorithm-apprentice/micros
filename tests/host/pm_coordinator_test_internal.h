#ifndef MICROS_TESTS_HOST_PM_COORDINATOR_TEST_INTERNAL_H
#define MICROS_TESTS_HOST_PM_COORDINATOR_TEST_INTERNAL_H

#include <stdio.h>
#include <string.h>

#include "servers/pm/pm_coordinator.h"
#include "micros/grant.h"
#include "micros/vfs.h"
#include "micros/vm_service.h"

#define COORDINATOR_EXPECT(expression) \
    do { \
        if (!(expression)) { \
            fprintf(stderr, "%s:%d: expected %s\n", \
                    __FILE__, __LINE__, #expression); \
            return false; \
        } \
    } while (false)

bool test_coordinator_initialization(void);
bool test_coordinator_embedded_bindings(void);
bool test_coordinator_binding_failures(void);
bool test_coordinator_table_failures(void);
bool test_coordinator_vfs_control_contract(void);
bool test_coordinator_vm_control_contract(void);
bool test_coordinator_control_rejections(void);
bool test_coordinator_directed_init(void);
bool test_coordinator_init_failure_rollback(void);
bool test_coordinator_init_postpublication_failures(void);
bool test_coordinator_init_preflight_preserves_pid(void);
bool test_coordinator_public_spawn(void);
bool test_coordinator_public_failure_results(void);
bool test_coordinator_public_preflight_results(void);
bool test_coordinator_parent_loss_rollback(void);
bool test_coordinator_public_capacity_and_unknown_grant(void);
bool test_coordinator_parent_loss_after_publication(void);

#endif
