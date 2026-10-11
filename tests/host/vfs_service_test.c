#include "tests/host/vfs_service_test_fixture.h"
#include "tests/host/vfs_service_test_suites.h"

int main(void) {
    struct vfs_service_test_context context;

    vfs_service_test_context_initialize(&context);
    return (vfs_service_public_constants_run(&context) &&
            vfs_service_pm_control_protocol_run(&context) &&
            vfs_service_application_protocol_run(&context) &&
            vfs_service_backend_protocol_run(&context) && vfs_service_configuration_run(&context) &&
            vfs_service_dispatch_run(&context))
               ? 0
               : 1;
}
