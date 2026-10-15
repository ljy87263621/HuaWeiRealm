#include <assert.h>

#include "lvgl.h"

int main(void)
{
    lv_image_header_t header;

    lv_init();
    assert(lv_image_decoder_get_info("A:./Emojis/red_heart.png", &header) == LV_RESULT_OK);
    assert(header.w > 0 && header.h > 0);
    lv_deinit();
    return 0;
}
