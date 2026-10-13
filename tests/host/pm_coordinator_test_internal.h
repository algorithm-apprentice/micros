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

#endif
