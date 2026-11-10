#ifndef MICROS_COMMON_SCHEDULER_RETURN_WORK_H
#define MICROS_COMMON_SCHEDULER_RETURN_WORK_H

#if defined(MICROS_BUILD_SCHEDULER_TEST)

#include "micros/kernel_objects.h"

enum micros_obj02_return_mode {
    MICROS_OBJ02_RETURN_START,
    MICROS_OBJ02_RETURN_USER,
    MICROS_OBJ02_RETURN_CAPTURED,
    MICROS_OBJ02_RETURN_IDLE,
};

void micros_obj02_return_begin(
    enum micros_obj02_return_mode mode,
    struct micros_thread_handle outgoing
);
void micros_obj02_return_validation(void);
void micros_obj02_return_end(struct micros_thread_handle selected);
void micros_obj02_return_cancel(void);
bool micros_obj02_return_report(void);

#define MICROS_OBJ02_RETURN_BEGIN(mode, outgoing) \
    micros_obj02_return_begin((mode), (outgoing))
#define MICROS_OBJ02_RETURN_END(selected) micros_obj02_return_end((selected))
#define MICROS_OBJ02_RETURN_CANCEL() micros_obj02_return_cancel()
#define MICROS_OBJ02_VALIDATE_CONTEXT(thread, context) \
    (micros_obj02_return_validation(), micros_user_execution_validate_context((thread), (context)))

#else

#define MICROS_OBJ02_RETURN_BEGIN(mode, outgoing) ((void)0)
#define MICROS_OBJ02_RETURN_END(selected) ((void)0)
#define MICROS_OBJ02_RETURN_CANCEL() ((void)0)
#define MICROS_OBJ02_VALIDATE_CONTEXT(thread, context) \
    micros_user_execution_validate_context((thread), (context))

#endif

#endif
