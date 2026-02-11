#include "LVGL_Driver.h"
#include "display/lv_display.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "portmacro.h"
#include "st7789.h"
#include "tick/lv_tick.h"
#include "widgets/label/lv_label.h"
#include <cstddef>
#include <cstdint>

static const char *TAG_LVGL = "WS_LVGL";
static const size_t BUF_SIZE = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * 2;
static uint8_t buf[BUF_SIZE];

// static lv_color_t buf1[LVGL_BUF_LEN];
// static lv_color_t buf2[LVGL_BUF_LEN];
// static lv_color_t* buf1 = (lv_color_t*) heap_caps_malloc(LVGL_BUF_LEN ,
// MALLOC_CAP_SPIRAM); static lv_color_t* buf2 = (lv_color_t*)
// heap_caps_malloc(LVGL_BUF_LEN , MALLOC_CAP_SPIRAM);

// lv_disp_draw_buf_t
//     disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
// lv_disp_drv_t disp_drv; // contains callback functions
//
void example_increase_lvgl_tick(void *arg) {
  /* Tell LVGL how many milliseconds has elapsed */
  // lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io,
                                     esp_lcd_panel_io_event_data_t *edata,
                                     void *user_ctx) {
  // lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
  // lv_disp_flush_ready(disp_driver);
  return false;
}

void example_lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                           uint8_t *color_map) {
  //  esp_lcd_panel_handle_t panel_handle =
  //  (esp_lcd_panel_handle_t)disp->user_data;
  int offsetx1 = area->x1;
  int offsetx2 = area->x2;
  int offsety1 = area->y1;
  int offsety2 = area->y2;
  // copy a buffer's content to a specific area of the display
  esp_lcd_panel_draw_bitmap(panel_handle, offsetx1 + Offset_X,
                            offsety1 + Offset_Y, offsetx2 + Offset_X + 1,
                            offsety2 + Offset_Y + 1, color_map);
  lv_display_flush_ready(disp);
}

/* Rotate display and touch, when rotated screen in LVGL. Called when driver
 * parameters are updated. */
void example_lvgl_port_update_callback(_lv_display_t *drv) {
  // esp_lcd_panel_handle_t panel_handle =
  // (esp_lcd_panel_handle_t)drv->user_data;
  //
  // switch (drv->rotated) {
  // case LV_DISP_ROT_NONE:
  //   // Rotate LCD display
  //   esp_lcd_panel_swap_xy(panel_handle, false);
  //   esp_lcd_panel_mirror(panel_handle, true, false);
  //   break;
  // case LV_DISP_ROT_90:
  //   // Rotate LCD display
  //   esp_lcd_panel_swap_xy(panel_handle, true);
  //   esp_lcd_panel_mirror(panel_handle, true, true);
  //   break;
  // case LV_DISP_ROT_180:
  //   // Rotate LCD display
  //   esp_lcd_panel_swap_xy(panel_handle, false);
  //   esp_lcd_panel_mirror(panel_handle, false, true);
  //   break;
  // case LV_DISP_ROT_270:
  //   // Rotate LCD display
  //   esp_lcd_panel_swap_xy(panel_handle, true);
  //   esp_lcd_panel_mirror(panel_handle, false, false);
  //   break;
  // }
}

uint32_t get_ms_since_startup() {
  static uint32_t counter = 0;
  ++counter;
  ESP_LOGI(TAG_LVGL, "get_ms_since_startup(): %d", counter);
  return counter;
}

void lcd_task(void *params) {
  /* Make LVGL periodically execute its tasks */
  while (1) {
    /* Provide updates to currently-displayed Widgets here. */
    lv_timer_handler();
    vTaskDelay(5 / portTICK_PERIOD_MS); /*Wait 5 milliseconds before processing
                                            LVGL timer again*/
  }
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
lv_display_t *disp;
void LVGL_Init(void) {
  ESP_LOGI(TAG_LVGL, "Initialize LVGL library");
  lv_init();
  lv_tick_set_cb(get_ms_since_startup);

  ESP_LOGI(TAG_LVGL, "Register display driver to LVGL");
  disp = lv_display_create(EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);
  lv_display_set_buffers(disp, buf, NULL, BUF_SIZE,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(disp, example_lvgl_flush_cb);
  lv_obj_t *label = lv_label_create(lv_screen_active());
  lv_label_set_text(label, "Hello LVGL!");
  TaskHandle_t lcdTask = NULL;
  xTaskCreate(lcd_task, "LVGL", 8, NULL, tskIDLE_PRIORITY, &lcdTask);
  // lv_disp_drv_init(&disp); // Create a new screen object and initialize the
  //                              // associated device
  // disp_drv.hor_res = EXAMPLE_LCD_H_RES;
  // disp_drv.ver_res = EXAMPLE_LCD_V_RES; // Horizontal pixel count
  // // disp_drv.rotated = LV_DISP_ROT_90; // 图像旋转 // Vertical axis pixel
  // count disp_drv.flush_cb =
  //     example_lvgl_flush_cb; // Function : copy a buffer's content to a
  //     specific
  //                            // area of the display
  // disp_drv.drv_update_cb =
  //     example_lvgl_port_update_callback; // Function : Rotate display and
  //     touch,
  //                                        // when rotated screen in LVGL.
  //                                        Called
  //                                        // when driver parameters are
  //                                        updated.
  // disp_drv.draw_buf =
  //     &disp_buf; // LVGL will use this buffer(s) to draw the screens contents
  // disp_drv.user_data = panel_handle;
  // ESP_LOGI(TAG_LVGL,
  //          "Register display indev to LVGL"); // Custom display driver user
  //          data
  // disp = lv_disp_drv_register(&disp_drv);     // Create screen objects

  /********************* LVGL *********************/
  ESP_LOGI(TAG_LVGL, "Install LVGL tick timer");
  // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
  // const esp_timer_create_args_t lvgl_tick_timer_args = {
  //     .callback = &example_increase_lvgl_tick, .name = "lvgl_tick"};
  //
  // esp_timer_handle_t lvgl_tick_timer = NULL;
  // ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
  // ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer,
  //                                          EXAMPLE_LVGL_TICK_PERIOD_MS *
  //                                          1000));
}
