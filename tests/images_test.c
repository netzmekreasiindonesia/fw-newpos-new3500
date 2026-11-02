#include <assert.h>
#include <stdio.h>
#include "soundbox_images.h"

static void pixel(const lv_img_dsc_t *image, unsigned x, unsigned y,
    unsigned red, unsigned green, unsigned blue, unsigned alpha)
{
    const uint8_t *p = image->data +
        (y * image->header.w + x) * LV_IMG_PX_SIZE_ALPHA_BYTE;
#if LV_COLOR_DEPTH == 16
    unsigned rgb565 = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3);
    assert(p[0] == (rgb565 & 255) && p[1] == (rgb565 >> 8));
    assert(p[2] == alpha);
#else
    assert(p[0] == blue && p[1] == green && p[2] == red && p[3] == alpha);
#endif
}

int main(void)
{
    static const lv_img_dsc_t *const images[] = {
        &soundbox_img_motor, &soundbox_img_mobil, &soundbox_img_sepeda,
        &soundbox_img_elf, &soundbox_img_bus, &soundbox_img_calculator,
        &soundbox_img_gearblack, &soundbox_img_history, &soundbox_img_btn_back,
        &soundbox_img_netzme_name, &soundbox_img_netzme_small,
        &soundbox_img_qris_white, &soundbox_img_qris_black,
        &soundbox_img_qris_tap_white, &soundbox_img_qris_tap_black,
        &soundbox_img_batalkan, &soundbox_img_wait_nfc,
        &soundbox_img_success_payment, &soundbox_img_failed_payment,
        &soundbox_img_tokonetzme
    };
    size_t i;
    for (i = 0; i < sizeof(images) / sizeof(images[0]); ++i) {
        assert(images[i]->header.cf == LV_IMG_CF_TRUE_COLOR_ALPHA);
        assert(images[i]->header.always_zero == 0 && images[i]->header.reserved == 0);
        assert(images[i]->data_size ==
            images[i]->header.w * images[i]->header.h * LV_IMG_PX_SIZE_ALPHA_BYTE);
    }
    /* RGBA samples read independently from the original DQ72 PNGs. */
    pixel(&soundbox_img_motor, 20, 20, 249, 249, 249, 255);
    pixel(&soundbox_img_motor, 0, 0, 255, 255, 255, 0);
    pixel(&soundbox_img_success_payment, 72, 35, 0, 211, 255, 255);
    pixel(&soundbox_img_success_payment, 72, 68, 254, 255, 255, 255);
    pixel(&soundbox_img_success_payment, 120, 80, 220, 234, 243, 252);
    pixel(&soundbox_img_failed_payment, 73, 35, 255, 59, 101, 255);
    pixel(&soundbox_img_wait_nfc, 70, 38, 200, 216, 222, 255);
    printf("DQ72 image descriptors/pixels passed at %d-bit color\n", LV_COLOR_DEPTH);
    return 0;
}
