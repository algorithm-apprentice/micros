#ifndef MICROS_TESTS_HOST_VFS_MODEL_INTERNAL_H
#define MICROS_TESTS_HOST_VFS_MODEL_INTERNAL_H

#include <stdbool.h>

struct vfs_model;

bool vfs_model_execute_step(struct vfs_model *model);
bool vfs_model_coverage_is_complete(struct vfs_model *model);

#endif
