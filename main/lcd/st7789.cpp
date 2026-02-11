#include "st7789.h"
#include "driver/gpio.h"
#include "esp_lcd_types.h"
#include "hal/lcd_types.h"
#include "hal/spi_types.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

static const char *TAG_LCD = "WS_LCD";
#define LCD_SPI_HOST_ID SPI2_HOST

esp_lcd_panel_handle_t panel_handle = NULL;

static const size_t size =
    EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * sizeof(uint16_t);

void draw_example_label() {
  uint8_t *img = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_DMA);

  esp_lcd_panel_reset(panel_handle);
  esp_lcd_panel_init(panel_handle);
  esp_lcd_panel_invert_color(panel_handle, true);
  // the gap is LCD panel specific, even panels with the same driver IC, can
  // have different gap value
  esp_lcd_panel_set_gap(panel_handle, 0, 20);
  // turn on display
  esp_lcd_panel_disp_on_off(panel_handle, true);
  memset(img, 0x3f, size);
  ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(
      panel_handle, 0, 0, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES, img));
  free(img);
}

bool on_color_trans_done(esp_lcd_panel_handle_t) { return true; }

void LCD_Init(void) {
  ESP_LOGI(TAG_LCD, "Initialize SPI bus");
  spi_bus_config_t buscfg;
  memset(&buscfg, 0, sizeof(spi_bus_config_t));
  buscfg.sclk_io_num = EXAMPLE_PIN_NUM_SCLK;
  buscfg.mosi_io_num = EXAMPLE_PIN_NUM_MOSI;
  buscfg.miso_io_num = -1;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz =
      EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * sizeof(uint16_t);
  ESP_ERROR_CHECK(
      spi_bus_initialize(LCD_SPI_HOST_ID, &buscfg, SPI_DMA_CH_AUTO));

  ESP_LOGI(TAG_LCD, "Install panel IO");
  esp_lcd_panel_io_handle_t io_handle = NULL;
  esp_lcd_panel_io_spi_config_t io_config;
  io_config.dc_gpio_num = EXAMPLE_PIN_NUM_LCD_DC;
  io_config.cs_gpio_num = EXAMPLE_PIN_NUM_LCD_CS;
  io_config.pclk_hz = EXAMPLE_LCD_PIXEL_CLOCK_HZ;
  io_config.lcd_cmd_bits = EXAMPLE_LCD_CMD_BITS;
  io_config.lcd_param_bits = EXAMPLE_LCD_PARAM_BITS;
  io_config.spi_mode = 0;
  io_config.trans_queue_depth = 10;
  io_config.on_color_trans_done = NULL; // on_color_trans_done;
  io_config.user_ctx = NULL;
  // Attach the LCD to the SPI bus
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
      (esp_lcd_spi_bus_handle_t)LCD_SPI_HOST_ID, &io_config, &io_handle));

  esp_lcd_panel_dev_config_t panel_config;
  panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
  panel_config.bits_per_pixel = 16;
  panel_config.reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST;
  // Create LCD panel handle for ST7789, with the SPI IO device handle
  ESP_ERROR_CHECK(
      esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

  // esp_lcd_panel_dev_st7789t_config_t panel_config;
  // panel_config.reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST;
  // panel_config.rgb_endian = LCD_RGB_ELEMENT_ORDER_RGB;
  // panel_config.bits_per_pixel = 16;
  // ESP_LOGI(TAG_LCD, "Install ST7789T panel driver");
  // ESP_ERROR_CHECK(
  //     esp_lcd_new_panel_st7789t(io_handle, &panel_config, &panel_handle));

  // ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  // ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  // ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));
  //
  // // user can flush pre-defined pattern to the screen before we turn on the
  // // screen or backlight
  // ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

  // ESP_LOGI(TAG_LCD, "Turn on LCD backlight");
  //  gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);

  BK_Init(); // Initialize the backlight
  draw_example_label();
  //   BK_Light(75);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Backlight program
static ledc_channel_config_t ledc_channel;
void BK_Init(void) {
  ESP_LOGI(TAG_LCD, "Turn off LCD backlight");
  gpio_config_t bk_gpio_config;
  bk_gpio_config.mode = GPIO_MODE_OUTPUT;
  bk_gpio_config.pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT;
  gpio_set_direction(EXAMPLE_PIN_NUM_BK_LIGHT, GPIO_MODE_OUTPUT);
  ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

  // 配置LEDC
  // ledc_timer_config_t ledc_timer;
  // ledc_timer.duty_resolution = LEDC_TIMER_13_BIT;
  // ledc_timer.freq_hz = 5000;
  // ledc_timer.speed_mode = LEDC_LS_MODE;
  // ledc_timer.timer_num = LEDC_HS_TIMER;
  // ledc_timer.clk_cfg = LEDC_AUTO_CLK;
  // ledc_timer_config(&ledc_timer);
  //
  // ledc_channel.channel = LEDC_HS_CH0_CHANNEL;
  // ledc_channel.duty = 0;
  // ledc_channel.gpio_num = EXAMPLE_PIN_NUM_BK_LIGHT;
  // ledc_channel.speed_mode = LEDC_LS_MODE;
  // ledc_channel.timer_sel = LEDC_HS_TIMER;
  // ledc_channel_config(&ledc_channel);
  // ledc_fade_func_install(0);
}
void BK_Light(uint8_t Light) {
  if (Light > 0) {
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, 1);
  } else {
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, 0);
  }
  // if (Light > 100)
  //   Light = 100;
  // uint16_t Duty = LEDC_MAX_Duty - (81 * (100 - Light));
  // if (Light == 0)
  //   Duty = 0;
  // // 设置PWM占空比
  // ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, Duty);
  // ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
}
// end Backlight program
