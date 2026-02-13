#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_lcd_types.h"
#include "esp_log.h"
#include "hal/spi_types.h"
#include <cstdint>
#include <cstring>

#include "font8x8_basic.h"
#include "st7789.h"

static const char *TAG_LCD = "WS_LCD";
#define LCD_SPI_HOST_ID SPI2_HOST

esp_err_t Display::flush() {
  return esp_lcd_panel_draw_bitmap(panel_handle, 0, 0, W, H, img);
}

esp_err_t Display::rotate(ROTATION rot) {
  switch (rot) {
  case PORTRAIT: {
    if (esp_err_t err = esp_lcd_panel_swap_xy(panel_handle, false) != ESP_OK) {
      return err;
    }
    if (esp_err_t err =
            esp_lcd_panel_mirror(panel_handle, false, false) != ESP_OK) {
      return err;
    }
    W = EXAMPLE_LCD_H_RES;
    H = EXAMPLE_LCD_V_RES;
    // the gap is LCD panel specific, even panels with the same driver IC, can
    // have different gap value
    return esp_lcd_panel_set_gap(panel_handle, OFFSET_X, OFFSET_Y);
  } break;
  case LANDSCAPE: {
    if (esp_err_t err = esp_lcd_panel_swap_xy(panel_handle, true) != ESP_OK) {
      return err;
    }
    if (esp_err_t err =
            esp_lcd_panel_mirror(panel_handle, true, false) != ESP_OK) {
      return err;
    }
    W = EXAMPLE_LCD_V_RES;
    H = EXAMPLE_LCD_H_RES;
    // the gap is LCD panel specific, even panels with the same driver IC, can
    // have different gap value
    // -2 or less required to avoid warping of characters (why???)
    return esp_lcd_panel_set_gap(panel_handle, OFFSET_Y - 2, OFFSET_X);
  } break;
  }
  return ESP_FAIL; // default on unknown ROTATION
}

void Display::clear(uint16_t color) { memset(img, color, size); }

void Display::drawGlyph(const uint8_t *glyph, uint16_t x0, uint16_t y0,
                        uint8_t scale, uint16_t fg, uint16_t bg) {
  for (uint16_t y = 0; y < 8; ++y) {
    const uint8_t row = glyph[y];
    for (uint8_t sy = 0; sy < scale; ++sy) {
      const uint16_t Y = y0 + y * scale + sy;
      if (Y >= H) {
        continue;
      }
      for (uint16_t x = 0; x < 8; ++x) {
        const uint16_t color = (row & (1 << x)) ? fg : bg;
        for (uint8_t sx = 0; sx < scale; ++sx) {
          const uint16_t X = x0 + x * scale + sx;
          if (X < W) {
            img[Y * W + X] = color;
          }
        }
      }
    }
  }
}

void Display::drawChar(char c, uint16_t x0, uint16_t y0, uint8_t scale,
                       uint16_t fg, uint16_t bg) {
  const uint8_t *glyph = font8x8_basic[(uint8_t)c];
  drawGlyph(glyph, x0, y0, scale, fg, bg);
}

esp_err_t Display::drawString(const char *s, uint8_t len, uint16_t x0,
                              uint16_t y0, uint8_t scale, uint16_t fg,
                              uint16_t bg) {
  // will the string fit on the screen?
  if (x0 + len * 8 * scale >= W) {
    return ESP_FAIL;
  }
  if (y0 + 8 * scale >= H) {
    return ESP_FAIL;
  }
  for (uint8_t i = 0; i < len; ++i) {
    drawChar(s[i], x0 + 8 * scale * i, y0, scale, fg, bg);
  }
  return ESP_OK;
}

Display::Display(ROTATION rot, uint8_t backlight)
    : W(EXAMPLE_LCD_H_RES), H(EXAMPLE_LCD_V_RES), dimensions(W * H),
      size(dimensions * sizeof(uint16_t)) {
  ESP_LOGI(TAG_LCD, "Size: %d", size);
  ESP_LOGI(TAG_LCD, "Initialize SPI bus");
  spi_bus_config_t buscfg;
  memset(&buscfg, 0, sizeof(spi_bus_config_t));
  buscfg.sclk_io_num = EXAMPLE_PIN_NUM_SCLK;
  buscfg.mosi_io_num = EXAMPLE_PIN_NUM_MOSI;
  buscfg.miso_io_num = -1;
  buscfg.quadwp_io_num = -1;
  buscfg.quadhd_io_num = -1;
  buscfg.max_transfer_sz = W * H * sizeof(uint16_t);
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
  this->initBacklight(); // Initialize the backlight
  this->setBacklight(backlight);
  ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
  ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
  ESP_ERROR_CHECK(rotate(rot));
  // esp_lcd_panel_set_gap(panel_handle, 50, 30);
  // turn on display
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
  ESP_LOGI(TAG_LCD, "LCD initialized");
  ESP_LOGI(TAG_LCD, "Init framebuffers...");
  // framebuffers need to get initialized after drivers!
  img = (uint16_t *)heap_caps_malloc(size, MALLOC_CAP_DMA);
  if (!img) {
    ESP_LOGE(TAG_LCD, "ERROR: Framebuffers could not get allocated!");
    return;
  }
  ESP_LOGI(TAG_LCD, "Framebuffers initialized");
}

Display &Display::instance(ROTATION initRot, uint8_t initBaclight) {
  static Display disp(initRot, initBaclight);
  return disp;
}

Display::~Display() { free(img); }

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Backlight program
void Display::initBacklight() {
  gpio_config_t bk_gpio_config;
  bk_gpio_config.mode = GPIO_MODE_OUTPUT;
  bk_gpio_config.pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT;
  ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));

  // LEDC
  ledc_timer_config_t ledc_timer;
  ledc_timer.duty_resolution = LEDC_TIMER_13_BIT;
  ledc_timer.freq_hz = 5000;
  ledc_timer.speed_mode = LEDC_LS_MODE;
  ledc_timer.timer_num = LEDC_HS_TIMER;
  ledc_timer.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&ledc_timer);

  ledc_channel.channel = LEDC_HS_CH0_CHANNEL;
  ledc_channel.duty = 0;
  ledc_channel.gpio_num = EXAMPLE_PIN_NUM_BK_LIGHT;
  ledc_channel.speed_mode = LEDC_LS_MODE;
  ledc_channel.timer_sel = LEDC_HS_TIMER;
  ledc_channel_config(&ledc_channel);
  ledc_fade_func_install(0);
}
void Display::setBacklight(uint8_t brightness) {
  if (brightness > 100) {
    brightness = 100;
  }
  uint16_t Duty = LEDC_MAX_Duty - (81 * (100 - brightness));
  if (brightness == 0) {
    Duty = 0;
  }
  ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, Duty);
  ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel);
}
// end Backlight program
