#include "bootstrap_control_test_internal.h"

#include "micros/sv39.h"
#include "micros/vm_bootstrap.h"

struct bootstrap_control_catalog_context {
    unsigned char text[MICROS_SV39_PAGE_SIZE];
    unsigned char rodata[MICROS_SV39_PAGE_SIZE];
    unsigned char data[MICROS_SV39_PAGE_SIZE];
};

static struct bootstrap_control_catalog_context catalog;

static void initialize_catalog(
    struct bootstrap_control_catalog_context *context,
    struct micros_bootstrap_image images[2]
)
{
    memset(context->text, 0x11, sizeof(context->text));
    memset(context->rodata, 0x22, sizeof(context->rodata));
    memset(context->data, 0, sizeof(context->data));
    memset(images, 0, 2 * sizeof(images[0]));
    images[0] = (struct micros_bootstrap_image){
        .version = MICROS_BOOTSTRAP_IMAGE_VERSION,
        .image_id = 101,
        .entry = MICROS_USER_VIRTUAL_BASE,
        .segment_count = MICROS_BOOTSTRAP_IMAGE_SEGMENT_COUNT,
        .segments = {
            {
                .virtual_address = MICROS_USER_VIRTUAL_BASE,
                .file_size = sizeof(context->text),
                .memory_size = sizeof(context->text),
                .flags = MICROS_BOOTSTRAP_IMAGE_READ
                    | MICROS_BOOTSTRAP_IMAGE_EXECUTE,
                .file_bytes = context->text,
            },
            {
                .virtual_address =
                    MICROS_USER_VIRTUAL_BASE
                    + MICROS_SV39_PAGE_SIZE,
                .file_size = sizeof(context->rodata),
                .memory_size = sizeof(context->rodata),
                .flags = MICROS_BOOTSTRAP_IMAGE_READ,
                .file_bytes = context->rodata,
            },
            {
                .virtual_address =
                    MICROS_USER_VIRTUAL_BASE
                    + 2 * MICROS_SV39_PAGE_SIZE,
                .file_size = sizeof(context->data),
                .memory_size = sizeof(context->data),
                .flags = MICROS_BOOTSTRAP_IMAGE_READ
                    | MICROS_BOOTSTRAP_IMAGE_WRITE,
                .file_bytes = context->data,
            },
        },
        .config_address =
            MICROS_USER_VIRTUAL_BASE
            + 2 * MICROS_SV39_PAGE_SIZE,
        .config_size =
            sizeof(struct micros_bootstrap_service_config),
        .page_count = 3,
        .image_end =
            MICROS_USER_VIRTUAL_BASE
            + 3 * MICROS_SV39_PAGE_SIZE,
    };
    images[1] = images[0];
    images[1].image_id = 102;
}

static bool accept_catalog_metadata(
    struct micros_bootstrap_image images[2],
    struct micros_bootstrap_image_info infos[2]
)
{
    EXPECT_TRUE(
        micros_bootstrap_image_catalog_validate(
            images,
            2,
            infos,
            2
        ) == MICROS_BOOTSTRAP_OK
        && infos[0].image_id == 101
        && infos[0].page_count == 3
        && infos[0].config_initially_zero
        && infos[1].image_id == 102
    );

    images[0].segments[2].memory_size =
        91 * MICROS_SV39_PAGE_SIZE;
    images[0].page_count = 93;
    images[0].image_end =
        MICROS_USER_VIRTUAL_BASE + 93 * MICROS_SV39_PAGE_SIZE;
    images[0].vm_boot_info_address =
        MICROS_USER_VIRTUAL_BASE + 3 * MICROS_SV39_PAGE_SIZE;
    images[0].vm_boot_info_size = MICROS_VM_BOOT_INFO_SIZE;
    EXPECT_TRUE(
        micros_bootstrap_image_catalog_validate(
            images,
            2,
            infos,
            2
        ) == MICROS_BOOTSTRAP_OK
        && infos[0].vm_boot_info_address
            == images[0].vm_boot_info_address
        && infos[0].vm_boot_info_size == MICROS_VM_BOOT_INFO_SIZE
        && infos[0].vm_boot_info_initially_zero
        && infos[1].vm_boot_info_address == 0
        && infos[1].vm_boot_info_size == 0
        && !infos[1].vm_boot_info_initially_zero
    );
    return true;
}

static bool reject_malformed_catalogs(
    struct bootstrap_control_catalog_context *context,
    struct micros_bootstrap_image images[2],
    struct micros_bootstrap_image_info infos[2],
    struct micros_bootstrap_image_info sentinel[2]
)
{
    images[0].vm_boot_info_address += 1;
    EXPECT_TRUE(
        micros_bootstrap_image_catalog_validate(
            images,
            2,
            infos,
            2
        ) == MICROS_BOOTSTRAP_ERROR_IMAGE
    );
    images[0].vm_boot_info_address -= 1;
    images[0].vm_boot_info_size = MICROS_VM_BOOT_INFO_SIZE - 1;
    EXPECT_TRUE(
        micros_bootstrap_image_catalog_validate(
            images,
            2,
            infos,
            2
        ) == MICROS_BOOTSTRAP_ERROR_IMAGE
    );
    images[0].vm_boot_info_size = MICROS_VM_BOOT_INFO_SIZE;
    images[0].vm_boot_info_address = images[0].config_address;
    EXPECT_TRUE(
        micros_bootstrap_image_catalog_validate(
            images,
            2,
            infos,
            2
        ) == MICROS_BOOTSTRAP_ERROR_IMAGE
    );
    images[0].vm_boot_info_address =
        MICROS_USER_VIRTUAL_BASE + 3 * MICROS_SV39_PAGE_SIZE;

    memset(sentinel, 0xa5, 2 * sizeof(sentinel[0]));
    memcpy(infos, sentinel, 2 * sizeof(infos[0]));
    images[1].image_id = 101;
    EXPECT_TRUE(
        micros_bootstrap_image_catalog_validate(
            images,
            2,
            infos,
            2
        ) == MICROS_BOOTSTRAP_ERROR_IMAGE
        && memcmp(infos, sentinel, 2 * sizeof(infos[0])) == 0
    );
    images[1].image_id = 102;
    images[0].segments[0].flags |= MICROS_BOOTSTRAP_IMAGE_WRITE;
    EXPECT_TRUE(
        micros_bootstrap_image_catalog_validate(
            images,
            2,
            infos,
            2
        ) == MICROS_BOOTSTRAP_ERROR_IMAGE
        && memcmp(infos, sentinel, 2 * sizeof(infos[0])) == 0
    );
    images[0].segments[0].flags =
        MICROS_BOOTSTRAP_IMAGE_READ
        | MICROS_BOOTSTRAP_IMAGE_EXECUTE;
    context->data[0] = 1;
    EXPECT_TRUE(
        micros_bootstrap_image_catalog_validate(
            images,
            2,
            infos,
            2
        ) == MICROS_BOOTSTRAP_ERROR_IMAGE
        && memcmp(infos, sentinel, 2 * sizeof(infos[0])) == 0
    );
    context->data[0] = 0;
    return true;
}

bool test_image_catalog(void)
{
    struct micros_bootstrap_image images[2];
    struct micros_bootstrap_image_info infos[2];
    struct micros_bootstrap_image_info sentinel[2];

    initialize_catalog(&catalog, images);
    return (
        accept_catalog_metadata(images, infos)
        && reject_malformed_catalogs(&catalog, images, infos, sentinel)
    );
}
