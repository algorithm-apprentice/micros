#include "bootstrap_control_test_internal.h"

int main(void)
{
    return (
        test_valid_commands()
        && test_malformed_commands()
        && test_binding_lookup_rejects_oversized_count()
        && test_control_transitions()
        && test_pm_completion_rejections_are_atomic()
        && test_completion_rejects_launcher_ipc_dependencies()
        && test_pm_completion_queues_sealed_event_once()
        && test_pm_completion_wakes_only_any_receiver()
        && test_pm_completion_preserves_specific_receiver()
        && test_image_catalog()
    )
        ? 0
        : 1;
}
