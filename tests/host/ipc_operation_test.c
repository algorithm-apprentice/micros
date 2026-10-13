#include "ipc_operation_test_internal.h"

bool micros_ipc_operation_test_run(void)
{
    struct ipc_operation_test_context *fixture = ipc_operation_test_context_get();
    return (
        test_authorized_send_blocks_with_snapshot(fixture)
        && test_authorized_send_matches_first_receiver(fixture)
        && test_send_rejections_are_atomic(fixture)
        && test_authorized_receive_blocks(fixture)
        && test_receive_specific_and_any_fifo(fixture)
        && test_receive_rejections_are_atomic(fixture)
    );
}
