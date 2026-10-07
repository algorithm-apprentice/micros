#include <stdbool.h>
#include <stdio.h>

bool micros_ipc_model_test_run(void);
bool micros_endpoint_model_test_run(void);
bool micros_ipc_close_model_test_run(void);
bool micros_ipc_notify_model_test_run(void);
bool micros_grant_model_test_run(void);

int main(void)
{
    static const struct {
        const char *name;
        bool (*run)(void);
    } tests[] = {
        {
            "endpoint lifecycle model",
            micros_endpoint_model_test_run,
        },
        {
            "notification model",
            micros_ipc_notify_model_test_run,
        },
        {
            "endpoint close scenarios",
            micros_ipc_close_model_test_run,
        },
        {
            "direct grant registry model",
            micros_grant_model_test_run,
        },
        {
            "persistent IPC acceptance model",
            micros_ipc_model_test_run,
        },
    };
    size_t index;

    for (index = 0; index < sizeof(tests) / sizeof(tests[0]); ++index) {
        if (!tests[index].run()) {
            fprintf(stderr, "not ok %zu - %s\n", index + 1, tests[index].name);
            return 1;
        }
        printf("ok %zu - %s\n", index + 1, tests[index].name);
    }
    return 0;
}
