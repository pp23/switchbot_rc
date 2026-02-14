#pragma once
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include <cstddef>
#include <cstdint>
#include <sys/types.h>

// LCD SPI GPIO
// Using SPI2
#define LCD_HOST SPI2_HOST

#define EXAMPLE_LCD_PIXEL_CLOCK_HZ (12 * 1000 * 1000)
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL 1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_SCLK 7
#define EXAMPLE_PIN_NUM_MOSI 6
#define EXAMPLE_PIN_NUM_LCD_CS ((gpio_num_t)14)
#define EXAMPLE_PIN_NUM_LCD_DC ((gpio_num_t)15)
#define EXAMPLE_PIN_NUM_LCD_RST ((gpio_num_t)21)
#define EXAMPLE_PIN_NUM_BK_LIGHT ((gpio_num_t)22)
// The pixel number in horizontal and vertical
#define EXAMPLE_LCD_H_RES 172
#define EXAMPLE_LCD_V_RES 335
// Bit number used to represent command and parameter
#define EXAMPLE_LCD_CMD_BITS 8
#define EXAMPLE_LCD_PARAM_BITS 8

#define OFFSET_X 34
#define OFFSET_Y -13

#define LEDC_HS_TIMER LEDC_TIMER_0
#define LEDC_LS_MODE LEDC_LOW_SPEED_MODE
#define LEDC_HS_CH0_GPIO EXAMPLE_PIN_NUM_BK_LIGHT
#define LEDC_HS_CH0_CHANNEL LEDC_CHANNEL_0
#define LEDC_TEST_DUTY (4000)
#define LEDC_ResolutionRatio LEDC_TIMER_13_BIT
#define LEDC_MAX_Duty ((1 << LEDC_ResolutionRatio) - 1)

class IDisplay {
public:
  virtual esp_err_t flush(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                          uint16_t *buf) = 0;
};

class Canvas {
public:
  Canvas(IDisplay *d, uint16_t *buf, uint16_t x0, uint16_t y0, uint16_t x1,
         uint16_t y1);
  ~Canvas() {}
  esp_err_t clear(uint16_t color = 0x0);
  esp_err_t drawString(const char *s, uint8_t len, uint16_t x0, uint16_t y0,
                       uint8_t scale, uint16_t fg);

protected:
  void drawChar(char c, uint16_t x0, uint16_t y0, uint8_t scale, uint16_t fg);
  void drawGlyph(const uint8_t *glyph, uint16_t x0, uint16_t y0, uint8_t scale,
                 uint16_t fg);

private:
  size_t size() const;
  uint16_t W() const;
  uint16_t H() const;
  IDisplay *_d;
  uint16_t *_buf;
  uint16_t _x0, _y0, _x1, _y1;
};

class Display : public IDisplay {
public:
  enum ROTATION {
    PORTRAIT = 0,
    LANDSCAPE = 90,
    // not implemented:
    // PORTRAIT_UPSIDEDOWN = 180,
    // LANDSCAPE_270 = 270
  };

  // singleton required to overcome init race conditions
  // if different peripherals get used
  // and to have a static single framebuffer
  static Display &instance(ROTATION initRot = PORTRAIT,
                           uint8_t initBaclight = 0);
  Display(const Display &) = delete;
  Display &operator=(const Display &) = delete;
  ~Display();
  //! occupies a memory range in the overall framebuffer that defines the area
  Canvas *createArea(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
  esp_err_t flush(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1,
                  uint16_t *buf) override;
  esp_err_t flush();

  esp_err_t rotate(ROTATION rot);
  void clear(uint16_t color = 0x0);
  void setBacklight(uint8_t brightness);

  uint16_t width() const;
  uint16_t height() const;

protected:
  Display(ROTATION rot = PORTRAIT, uint8_t backlight = 0);
  void initBacklight();

private:
  esp_lcd_panel_handle_t panel_handle;
  ledc_channel_config_t ledc_channel;
  uint16_t W;              // = EXAMPLE_LCD_H_RES;
  uint16_t H;              // = EXAMPLE_LCD_V_RES;
  const size_t dimensions; // = W * H;
  const size_t size;       // = dimensions * sizeof(uint16_t);
  uint16_t *img = NULL;
  size_t _lastM1 = 0; // last area upper boundary memory index
};
