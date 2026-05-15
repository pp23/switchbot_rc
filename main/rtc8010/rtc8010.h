#ifndef _RTC8010_H_
#define _RTC8010_H_

#include "driver/i2c_types.h"
#include "esp_err.h"
#include "freertos/idf_additions.h"
#include "soc/gpio_num.h"

#include <sys/types.h>
#include <time.h>

typedef struct rtc8010_handle rtc8010_handle_t;
typedef void (*alarm_cb_t)(rtc8010_handle_t *rtc8010);

//! Defines the rtc8010 device
struct rtc8010_handle {
  QueueHandle_t evt_q; //! Queue handle to queue up rtc8010 alarm interrupts
  i2c_master_bus_handle_t i2c_master_handle; //! The I2C bus master handle
  i2c_master_dev_handle_t
      dev_handle;  //! The I2C bus device handle of the rtc8010
  gpio_num_t irq1; //! The pin connected to IRQ1 of the rtc8010
  alarm_cb_t alarm_cb;
};

// cron_format_t represents an equivalent syntax as in cron tabs
// supports same values as the rtc8010
// set 0xff for *
typedef struct {
  uint8_t min;     // 0-59
  uint8_t hour;    // 0-23
  uint8_t day;     // 1-31
  uint8_t weekday; // 0-6
} cron_format_t;

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

//! Configures I2C connection to a rtc8010 connected at given sda/scl pins
esp_err_t rtc8010_open(rtc8010_handle_t *handle, gpio_num_t sda,
                       gpio_num_t scl);
//! Runs init routine of the rtc8010 according to datasheet
esp_err_t rtc8010_init(rtc8010_handle_t *handle);
//! Sets the time of the rtc8010 according to given tm-struct
esp_err_t rtc8010_set_time(rtc8010_handle_t *handle, const struct tm *t);
//! Gets the time from rtc8010 and stores it in given tm-struct
esp_err_t rtc8010_get_time(rtc8010_handle_t *handle, struct tm *t);
//! Installs an ISR-service at given IRQ GPIO and inits the event queue
esp_err_t rtc8010_init_alarm(rtc8010_handle_t *handle, gpio_num_t irq,
                             alarm_cb_t alarm_cb);
//! Reset alarm and interrupt with given time.
//! If day and week is set to 0xff (or day==0), only min/hour used for alarm.
//! If weekday and day are set, weekday precedes days.
esp_err_t rtc8010_reset_alarm(rtc8010_handle_t *handle,
                              const cron_format_t *cron);
//! Closes the I2C connection to the rtc8010
esp_err_t rtc8010_close(rtc8010_handle_t *handle);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif
