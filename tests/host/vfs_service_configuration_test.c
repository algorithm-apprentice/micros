#include "tests/host/vfs_service_test_fixture.h"
#include "tests/host/vfs_service_test_suites.h"

#include "micros/spawn.h"
#include "micros/vfs.h"
#include "servers/vfs/vfs_service_core.h"

#include <stdio.h>
#include <string.h>

#define EXPECT_TRUE(expression)                                                                    \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            fprintf(stderr, "%s:%d: expected %s\n", __FILE__, __LINE__, #expression);              \
            return false;                                                                          \
        }                                                                                          \
    } while (false)

static bool production_configuration_is_exact(void) {
    struct micros_bootstrap_service_config config = vfs_service_test_production_config();
    struct micros_vfs_service_endpoints endpoints;

    EXPECT_TRUE(micros_vfs_service_validate_configuration(&config, &endpoints) &&
                endpoints.launcher == UINT32_C(0x00001000) &&
                endpoints.vm == UINT32_C(0x00001001) && endpoints.pm == UINT32_C(0x00001002) &&
                endpoints.tty == UINT32_C(0x00001003) && endpoints.ramfs == UINT32_C(0x00001004) &&
                endpoints.self == UINT32_C(0x00001005));
    config.service_count = MICROS_BOOTSTRAP_SERVICE_CAPACITY;
    EXPECT_TRUE(!micros_vfs_service_validate_configuration(&config, &endpoints));
    config = vfs_service_test_production_config();
    config.services[6].service_id = 7;
    config.services[6].endpoint = UINT32_C(0x00001006);
    EXPECT_TRUE(!micros_vfs_service_validate_configuration(&config, &endpoints));
    config = vfs_service_test_production_config();
    config.services[4].endpoint = config.services[3].endpoint;
    EXPECT_TRUE(!micros_vfs_service_validate_configuration(&config, &endpoints));
    config = vfs_service_test_production_config();
    config.reserved[0] = 1;
    EXPECT_TRUE(!micros_vfs_service_validate_configuration(&config, &endpoints));
    return true;
}

static bool test_configuration_is_exact(struct vfs_service_test_context *context) {
    struct micros_bootstrap_service_config config = vfs_service_test_config(context);
    struct micros_vfs_service_endpoints endpoints;
    micros_endpoint_t selected_application;

    EXPECT_TRUE(micros_vfs_service_validate_test_configuration(&config, &endpoints,
                                                               &selected_application) &&
                endpoints.launcher == UINT32_C(0x00001000) &&
                endpoints.vm == UINT32_C(0x00001001) && endpoints.pm == UINT32_C(0x00001002) &&
                endpoints.tty == UINT32_C(0x00001003) && endpoints.ramfs == UINT32_C(0x00001004) &&
                endpoints.self == UINT32_C(0x00001005) &&
                selected_application == context->application_endpoint &&
                !micros_vfs_service_validate_configuration(&config, &endpoints));
    config = vfs_service_test_production_config();
    EXPECT_TRUE(!micros_vfs_service_validate_test_configuration(&config, &endpoints,
                                                                &selected_application));
    config = vfs_service_test_config(context);
    config.service_count = MICROS_BOOTSTRAP_SERVICE_CAPACITY + 1;
    EXPECT_TRUE(!micros_vfs_service_validate_test_configuration(&config, &endpoints,
                                                                &selected_application));
    config = vfs_service_test_config(context);
    config.services[6].service_id = MICROS_VFS_SERVICE_ID;
    EXPECT_TRUE(!micros_vfs_service_validate_test_configuration(&config, &endpoints,
                                                                &selected_application));
    config = vfs_service_test_config(context);
    config.services[6].endpoint = config.services[5].endpoint;
    EXPECT_TRUE(!micros_vfs_service_validate_test_configuration(&config, &endpoints,
                                                                &selected_application));
    config = vfs_service_test_config(context);
    config.self_endpoint = context->application_endpoint;
    EXPECT_TRUE(!micros_vfs_service_validate_test_configuration(&config, &endpoints,
                                                                &selected_application));
    return true;
}

bool vfs_service_configuration_run(struct vfs_service_test_context *context) {
    return (production_configuration_is_exact() && test_configuration_is_exact(context));
}
