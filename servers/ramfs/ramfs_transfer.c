#include "servers/ramfs/ramfs_internal.h"

enum micros_ramfs_copy_result ramfs_copy_grant(
    const struct micros_ramfs_state *state,
    const struct micros_ramfs_io *io,
    enum micros_ramfs_copy_direction direction,
    micros_grant_t grant,
    uint64_t grant_offset,
    uint8_t *local,
    size_t length
)
{
    if (io == NULL || io->copy == NULL) {
        return MICROS_RAMFS_COPY_INVARIANT;
    }
    return io->copy(
        io->context,
        direction,
        state->vfs_endpoint,
        grant,
        grant_offset,
        local,
        length
    );
}
