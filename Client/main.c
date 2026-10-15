/**
 * @file main
 *
 */

/*********************
 *      INCLUDES
 *********************/
#define _DEFAULT_SOURCE /* needed for usleep() */
#include <stdlib.h>
#include <unistd.h>
#include "lvgl/lvgl.h"
#include "mycode/chat_frontend.h"

#ifdef CHAT_BOARD_FBDEV
#include "lvgl/src/drivers/display/fb/lv_linux_fbdev.h"
#include "lvgl/src/drivers/evdev/lv_evdev.h"
#endif

/* Forward declaration for mycode */
void obj_test(void);
void Sudoku(void);
void drag_obj_demo(void);
void lv_mem();
void container_demo();
/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static lv_display_t * hal_init(int32_t w, int32_t h);
static void clear_display_before_exit(void);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *      VARIABLES
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

int main(int argc, char **argv)
{
  (void)argc; /*Unused*/
  (void)argv; /*Unused*/

  /*Initialize LVGL*/
  lv_init();

  /*Initialize the display, and the input devices*/
  hal_init(1024, 600);
  // obj_test();
  // Sudoku();
  // drag_obj_demo();
  // lv_mem();
  chat_frontend_init();
  /*To hide the memory and performance indicators in the corners
   *disable `LV_USE_MEM_MONITOR` and `LV_USE_PERF_MONITOR` in `lv_conf.h`*/

  while(!chat_frontend_exit_requested()) {
      /* Periodically call the lv_task handler.
       * It could be done in a timer interrupt or an OS task too.*/
      lv_timer_handler();
      usleep(10 * 1000);
  }

  chat_frontend_shutdown();
  clear_display_before_exit();
  lv_deinit();
  return 0;
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Initialize the Hardware Abstraction Layer (HAL) for the LVGL graphics
 * library
 */
static lv_display_t * hal_init(int32_t w, int32_t h)
{
  lv_group_set_default(lv_group_create());

#ifdef CHAT_BOARD_FBDEV
  (void)w;
  (void)h;
  lv_display_t * disp = lv_linux_fbdev_create();
  lv_linux_fbdev_set_file(disp, "/dev/fb0");
  lv_indev_t * touch = lv_evdev_create(LV_INDEV_TYPE_POINTER, "/dev/input/event6");
  if(touch) {
    lv_indev_set_group(touch, lv_group_get_default());
    lv_indev_set_display(touch, disp);
  }
  lv_display_set_default(disp);
  return disp;
#else
  lv_display_t * disp = lv_sdl_window_create(w, h);

  lv_indev_t * mouse = lv_sdl_mouse_create();
  lv_indev_set_group(mouse, lv_group_get_default());
  lv_indev_set_display(mouse, disp);
  lv_display_set_default(disp);

  LV_IMAGE_DECLARE(mouse_cursor_icon); /*Declare the image file.*/
  lv_obj_t * cursor_obj;
  cursor_obj = lv_image_create(lv_screen_active()); /*Create an image object for the cursor */
  lv_image_set_src(cursor_obj, &mouse_cursor_icon);           /*Set the image source*/
  lv_indev_set_cursor(mouse, cursor_obj);             /*Connect the image  object to the driver*/

  lv_indev_t * mousewheel = lv_sdl_mousewheel_create();
  lv_indev_set_display(mousewheel, disp);

  lv_indev_t * keyboard = lv_sdl_keyboard_create();
  lv_indev_set_display(keyboard, disp);
  lv_indev_set_group(keyboard, lv_group_get_default());

  return disp;
#endif
}

static void clear_display_before_exit(void)
{
  lv_obj_t *blank_screen = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(blank_screen, lv_color_hex(0x000000), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(blank_screen, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_clean(lv_layer_bottom());
  lv_obj_clean(lv_layer_top());
  lv_obj_clean(lv_layer_sys());
  lv_screen_load(blank_screen);
  lv_refr_now(NULL);
}
