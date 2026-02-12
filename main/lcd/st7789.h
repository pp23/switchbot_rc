#pragma once
#include "esp_lcd_panel_io.h"

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

extern esp_lcd_panel_handle_t panel_handle;

void BK_Init(void); // Initialize the LCD backlight, which has been called in
                    // the LCD_Init function, ignore it
void BK_Light(uint8_t Light); // Call this function to adjust the brightness of
                              // the backlight. The value of the parameter Light
                              // ranges from 0 to 100

void LCD_Init(void); // Call this function to initialize the screen (must be
                     // called in the main function) !!!!!
