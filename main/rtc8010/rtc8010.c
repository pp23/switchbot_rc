#include "rtc8010.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2c_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include <math.h>

#define RTC_SDL_FREQ_HZ 100000
#define RTC_DEVICE_ADDR 0x32

#define RTC_BIT_VFL 0x1 // VFL bit 1 in register 0x1E
#define RTC_BIT_TE 0x4
#define RTC_BIT_AIE 0x3
#define RTC_BIT_TIE 0x4
#define RTC_BIT_UIE 0x5
#define RTC_BIT_TEST 0x7
#define RTC_BIT_AE 0x7
#define RTC_BIT_AF 0x3
#define RTC_BIT_WADA 0x3

static const uint8_t flagReg = 0x1E; // contains the VLF bit
static const uint8_t extReg = 0x1D;
static const uint8_t ctrlReg = 0x1F;
static const uint8_t secReg = 0x10;
static const uint8_t minReg = 0x11;
static const uint8_t hourReg = 0x12;
static const uint8_t weekReg = 0x13;
static const uint8_t dayReg = 0x14;
static const uint8_t monthReg = 0x15;
static const uint8_t yearReg = 0x16;
static const uint8_t minAlarmReg = 0x18;
static const uint8_t hourAlarmReg = 0x19;
static const uint8_t wadaReg = 0x1A;

//! Defines a I2C register address followed by a data byte to write to this
//! register
typedef union i2c_register {
  uint16_t data;
  struct {
    uint8_t addr;
    uint8_t byte;
  } register_data;
} i2c_register_t;

//! Returns the BCD encoded byte of a binary input byte
uint8_t bin2bcd(uint8_t bin) {
  return (uint8_t)(((bin / 10) << 4) | ((bin % 10) & 0xf));
}

//! Returns the binary byte of a BCD encoded input byte
uint8_t bcd2bin(uint8_t bcd) {
  return (uint8_t)(((bcd >> 4) * 10) + (bcd & 0xf));
}

//! Writes a given value at a given register address of a given I2C device
esp_err_t writeRegister(i2c_master_dev_handle_t dev_handle, uint8_t regAddr,
                        uint8_t value) {
  i2c_register_t regData = {
      .register_data = {.addr = regAddr, .byte = value},
  };
  ESP_LOGI("rtc", "Reg-Write: %04x", regData);
  return i2c_master_transmit(dev_handle, (const uint8_t *)&regData,
                             sizeof(i2c_register_t), 100);
}

void reset_irq(const rtc8010_handle_t *handle) {
  // reset IRQ
  esp_err_t ret = ESP_OK;
  uint8_t flags = 0;
  if ((ret = i2c_master_transmit_receive(handle->dev_handle, &flagReg, 1,
                                         &flags, 1, 10)) != ESP_OK) {
    ESP_LOGE("rtc", "could not read alarm register: %d", ret);
  }
  if ((ret = writeRegister(handle->dev_handle, flagReg,
                           flags & ~(1 << RTC_BIT_AF))) != ESP_OK) {
    ESP_LOGE("rtc", "could not reset alarm register: %d", ret);
  }
}

void IRAM_ATTR rtc_irq1_handler(void *arg) {
  rtc8010_handle_t *handle = (rtc8010_handle_t *)arg;
  xQueueSendFromISR(handle->evt_q, (void *)handle, NULL);
}
void rtc_irq1_task(void *arg) {
  QueueHandle_t *evt_q = (QueueHandle_t *)arg;
  rtc8010_handle_t handle;
  while (1) {
    if (xQueueReceive(*evt_q, &handle, 0)) {
      // reset IRQ
      reset_irq(&handle);
      handle.alarm_cb(&handle);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

esp_err_t rtc8010_open(rtc8010_handle_t *handle, gpio_num_t sda,
                       gpio_num_t scl) {
  assert(handle != NULL);
  esp_err_t ret = ESP_OK;
  // configure i2c
  i2c_master_bus_config_t master_cfg = {};
  master_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  master_cfg.i2c_port = I2C_NUM_0;
  master_cfg.sda_io_num = sda;
  master_cfg.scl_io_num = scl;
  master_cfg.glitch_ignore_cnt = 7;
  master_cfg.flags.enable_internal_pullup = true;
  if ((ret = i2c_new_master_bus(&master_cfg, &handle->i2c_master_handle)) !=
      ESP_OK) {
    return ret;
  }
  // test device connection
  if ((ret = i2c_master_probe(handle->i2c_master_handle, RTC_DEVICE_ADDR,
                              -1)) != ESP_OK) {
    return ret;
  }
  i2c_device_config_t dev_conf = {};
  dev_conf.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_conf.device_address = RTC_DEVICE_ADDR;
  dev_conf.scl_speed_hz = RTC_SDL_FREQ_HZ;
  if ((ret = i2c_master_bus_add_device(handle->i2c_master_handle, &dev_conf,
                                       &handle->dev_handle)) != ESP_OK) {
    return ret;
  }
  return ret;
}

esp_err_t rtc8010_init(rtc8010_handle_t *handle) {
  esp_err_t ret = ESP_OK;
  uint8_t data = 0;
  // read VLF bit
  i2c_master_transmit_receive(handle->dev_handle, &flagReg, 1, &data, 1, 100);
  ESP_LOGI("rtc", "data: %02x", data);
  if (data & RTC_BIT_VFL) {
    ESP_LOGI("rtc", "VFL flag set");
  }
  ESP_LOGI("rtc", "data: %02x", data);
  // init rtc
  i2c_register_t initData[] = {
      {.register_data = {.addr = 0x17, .byte = 0xD8}},
      {.register_data = {.addr = 0x30, .byte = 0x0}},
      {.register_data = {.addr = 0x31, .byte = 0x8}},
      {.register_data = {.addr = 0x32, .byte = 0x0}},
  };
  for (i2c_register_t *d = &initData[0];
       d != &initData[sizeof(initData) / sizeof(i2c_register_t)]; ++d) {
    ESP_LOGI("rtc", "init: %04x", *d);
    // TODO: use bulk transmit
    if ((ret = i2c_master_transmit(handle->dev_handle, (const uint8_t *)d,
                                   sizeof(i2c_register_t), 100)) != ESP_OK) {
      return ret;
    }
  }
  // read TE bit from 0x1D
  data = 0;
  if ((ret = i2c_master_transmit_receive(handle->dev_handle, &extReg, 1, &data,
                                         1, 100)) != ESP_OK) {
    return ret;
  }
  ESP_LOGI("rtc", "ExtensionRegister-Read: %02x", data);
  i2c_register_t rtc1D = {
      .register_data = {.addr = extReg,
                        .byte = (uint8_t)((data & ~(1 << RTC_BIT_TE)))}};
  ESP_LOGI("rtc", "ExtReg-Write: %04x", rtc1D);
  if ((ret = i2c_master_transmit(handle->dev_handle, (const uint8_t *)&rtc1D,
                                 sizeof(i2c_register_t), 100)) != ESP_OK) {
    return ret;
  }
  data = 0;
  if ((ret = i2c_master_transmit_receive(handle->dev_handle, &extReg, 1, &data,
                                         1, 100)) != ESP_OK) {
    return ret;
  }
  ESP_LOGI("rtc", "ExtensionRegister-Read: %02x", data);
  // clear VLF bit
  data = 0;
  i2c_master_transmit_receive(handle->dev_handle, &flagReg, 1, &data, 1, 100);
  i2c_register_t flags1E = {
      .register_data = {.addr = flagReg,
                        .byte = (uint8_t)((data & ~(1 << RTC_BIT_VFL)))}};
  if ((ret = i2c_master_transmit(handle->dev_handle, (const uint8_t *)&flags1E,
                                 sizeof(i2c_register_t), 100)) != ESP_OK) {
    return ret;
  }
  data = 0;
  if ((ret = i2c_master_transmit_receive(handle->dev_handle, &flagReg, 1, &data,
                                         1, 100)) != ESP_OK) {
    return ret;
  }
  ESP_LOGI("rtc", "flags: %02x", data);
  // clear TEST bit, AIE, TIE, UIE
  data = 0;
  if ((ret = i2c_master_transmit_receive(handle->dev_handle, &ctrlReg, 1, &data,
                                         1, 100)) != ESP_OK) {
    return ret;
  }
  i2c_register_t ctrl = {
      .register_data = {.addr = ctrlReg,
                        .byte =
                            (data & ~(1 << RTC_BIT_TEST | 1 << RTC_BIT_AIE |
                                      1 << RTC_BIT_TIE | 1 << RTC_BIT_UIE))}};
  ESP_LOGI("rtc", "CtrlReg-Write: %04x", ctrl);
  if ((ret = i2c_master_transmit(handle->dev_handle, (const uint8_t *)&ctrl,
                                 sizeof(i2c_register_t), 100)) != ESP_OK) {
    return ret;
  }
  // reset irq
  reset_irq(handle);
  return ret;
}

esp_err_t rtc8010_set_time(rtc8010_handle_t *handle, const struct tm *t) {
  assert(handle != NULL);
  assert(t != NULL);
  esp_err_t ret = ESP_OK;
  // TODO: use bulk write
  // set time
  if ((ret = writeRegister(handle->dev_handle, secReg, bin2bcd(t->tm_sec))) !=
      ESP_OK) {
    return ret;
  }
  if ((ret = writeRegister(handle->dev_handle, minReg, bin2bcd(t->tm_min))) !=
      ESP_OK) {
    return ret;
  }
  if ((ret = writeRegister(handle->dev_handle, hourReg, bin2bcd(t->tm_hour))) !=
      ESP_OK) {
    return ret;
  }
  // tm_wday days since sunday (0-6)
  if ((ret = writeRegister(handle->dev_handle, weekReg, (1 << t->tm_wday))) !=
      ESP_OK) {
    return ret;
  }
  if ((ret = writeRegister(handle->dev_handle, dayReg, bin2bcd(t->tm_mday))) !=
      ESP_OK) {
    return ret;
  }
  if ((ret = writeRegister(handle->dev_handle, monthReg, bin2bcd(t->tm_mon))) !=
      ESP_OK) {
    return ret;
  }
  // tm_year holds years since 1900, rtc8010 allows only values 0-99
  if ((ret = writeRegister(handle->dev_handle, yearReg,
                           bin2bcd(t->tm_year - 100))) != ESP_OK) {
    return ret;
  }
  return ret;
}

esp_err_t rtc8010_get_time(rtc8010_handle_t *handle, struct tm *t) {
  assert(handle != NULL);
  assert(t != NULL);
  esp_err_t ret = ESP_OK;
  // read from greatest to smallest unit to prevent time shift of t->tm_sec due
  // to read latency
  uint8_t data = 0;
  if ((ret = i2c_master_transmit_receive(handle->dev_handle, &yearReg, 1, &data,
                                         1, 100)) != ESP_OK) {
    return ret;
  }
  // tm_year: years since 1900, rtc8010 years 0-99
  t->tm_year = 100 + bcd2bin(data);
  data = 0;
  if ((ret = i2c_master_transmit_receive(handle->dev_handle, &monthReg, 1,
                                         &data, 1, 100)) != ESP_OK) {
    return ret;
  }
  t->tm_mon = bcd2bin(data);
  data = 0;
  if ((ret = i2c_master_transmit_receive(handle->dev_handle, &dayReg, 1, &data,
                                         1, 100)) != ESP_OK) {
    return ret;
  }
  t->tm_mday = bcd2bin(data);
  data = 0;
  if ((ret = i2c_master_transmit_receive(handle->dev_handle, &weekReg, 1, &data,
                                         1, 100)) != ESP_OK) {
    return ret;
  }
  t->tm_wday = log2(data);
  data = 0;
  if ((ret = i2c_master_transmit_receive(handle->dev_handle, &hourReg, 1, &data,
                                         1, 100)) != ESP_OK) {
    return ret;
  }
  t->tm_hour = bcd2bin(data);
  data = 0;
  if ((ret = i2c_master_transmit_receive(handle->dev_handle, &minReg, 1, &data,
                                         1, 100)) != ESP_OK) {
    return ret;
  }
  t->tm_min = bcd2bin(data);
  data = 0;
  if ((ret = i2c_master_transmit_receive(handle->dev_handle, &secReg, 1, &data,
                                         1, 100)) != ESP_OK) {
    return ret;
  }
  t->tm_sec = bcd2bin(data);
  return ret;
}

esp_err_t rtc8010_init_alarm(rtc8010_handle_t *handle, gpio_num_t irq,
                             alarm_cb_t alarm_cb) {
  assert(handle != NULL);
  assert(handle->dev_handle != NULL);
  assert(alarm_cb != NULL);
  esp_err_t ret = ESP_OK;
  // reset irq to avoid immediate triggering from last active alarm
  reset_irq(handle);
  // configure alarm isr
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_NEGEDGE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pin_bit_mask = (1ULL << irq);
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  if ((ret = gpio_config(&io_conf)) != ESP_OK) {
    return ret;
  }
  handle->evt_q = xQueueCreate(1, sizeof(rtc8010_handle_t));
  xTaskCreate(rtc_irq1_task, "rtc_alarm_task", 2048, (void *)&handle->evt_q, 10,
              NULL);
  if ((ret = gpio_set_intr_type(irq, GPIO_INTR_NEGEDGE)) != ESP_OK) {
    return ret;
  }
  if ((ret = gpio_install_isr_service(0)) != ESP_OK) {
    return ret;
  }
  handle->alarm_cb = alarm_cb;
  // add IRQ handle with dev_handle
  if ((ret = gpio_isr_handler_add(irq, rtc_irq1_handler, (void *)handle)) !=
      ESP_OK) {
    return ret;
  }
  return ret;
}

esp_err_t rtc8010_reset_alarm(rtc8010_handle_t *handle,
                              const cron_format_t *cron) {
  assert(handle != NULL);
  assert(cron != NULL);
  esp_err_t ret = ESP_OK;
  // reset irq to avoid immediate triggering from last active alarm
  reset_irq(handle);
  // set alarm
  // first disable AIE
  uint8_t data = 0;
  if ((ret = i2c_master_transmit_receive(handle->dev_handle, &ctrlReg, 1, &data,
                                         1, 10)) != ESP_OK) {
    return ret;
  }
  if ((ret = writeRegister(handle->dev_handle, ctrlReg,
                           data & ~(1 << RTC_BIT_AIE))) != ESP_OK) {
    return ret;
  }
  // set WADA AE to 1 or day or weekday if set (weekday precedes days)
  uint8_t wada = 0x80;
  uint8_t wadaFlag = 0;
  if (cron->day > 0 && cron->day < 32) {
    wada = bin2bcd(cron->day);
    wadaFlag = 1;
  }
  if (cron->weekday < 7) {
    wada = bin2bcd(cron->weekday);
    wadaFlag = 0;
  }
  if ((ret = writeRegister(handle->dev_handle, wadaReg, wada)) != ESP_OK) {
    return ret;
  }
  data = 0;
  if ((ret = i2c_master_transmit_receive(handle->dev_handle, &extReg, 1, &data,
                                         1, 10)) != ESP_OK) {
    return ret;
  }
  if ((ret = writeRegister(handle->dev_handle, extReg,
                           data & (wadaFlag << RTC_BIT_WADA))) != ESP_OK) {
    return ret;
  }
  // set hour/minute alarm
  if ((ret = writeRegister(handle->dev_handle, minAlarmReg,
                           cron->min == 0xff ? 0x80 : bin2bcd(cron->min))) !=
      ESP_OK) {
    return ret;
  }
  if ((ret = writeRegister(handle->dev_handle, hourAlarmReg,
                           cron->hour == 0xff ? 0x80 : bin2bcd(cron->hour))) !=
      ESP_OK) {
    return ret;
  }
  // enable AIE
  if ((ret = writeRegister(handle->dev_handle, ctrlReg,
                           data | (1 << RTC_BIT_AIE))) != ESP_OK) {
    return ret;
  }
  return ESP_OK;
}

esp_err_t rtc8010_close(rtc8010_handle_t *handle) {
  // TODO
  return ESP_OK;
}
