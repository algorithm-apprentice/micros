#include "user_address_space_core_test_suites.h"

bool micros_user_address_space_core_test_run(void) {
    return (micros_user_address_space_owner_test_run() &&
            micros_user_address_space_vm_plan_test_run());
}
