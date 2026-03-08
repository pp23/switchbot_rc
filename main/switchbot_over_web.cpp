// #include "bt/host/nimble/esp-hci/include/esp_nimble_hci.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif_ip_addr.h"
#include "esp_netif_sntp.h"
#include "esp_netif_types.h"
#include "esp_sleep.h"
#include "esp_sntp.h"
#include "freertos/idf_additions.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <netdb.h>
#include <sys/_timeval.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "device/ble_device_controller.h"
#include "lcd/st7789.h"
#include "portmacro.h"
#include "shutterbutton.h"
#include "switchbot.h"
#include "switchbot_net.h"

#define ABSOLUTE(x) ((x) < 0 ? -(x) : (x))

#define MS_TO_USEC(x) ((uint64_t)(x) * 1000)
#define S_TO_USEC(x) (MS_TO_USEC((x) * 1000))
#define M_TO_USEC(x) (S_TO_USEC((x) * 60))
#define H_TO_USEC(x) (M_TO_USEC((x) * 60))

#define DEEP_SLEEP_ENABLED
#define WIFI_ENABLED

static const char *tag = "main";

static esp_ip4_addr_t WIFI_IPV4;
static int8_t WIFI_RSSI = 0;
static bool WIFI_CONNECTED = false;
static bool NTP_SYNCED = false;
static bool SWITCHBOT_UPDATE = false;
static uint8_t SWITCHBOT_BAT = 0;

/****** DEEP SLEEP CONFIGURATION ******/
static const uint8_t SLEEP_START_HOUR = 16; // start deep sleep at 16h UTC
static const uint8_t SLEEP_END_HOUR = 6;    // end deep sleep at 6h UTC

void sleep_task(void *params) {
  time_t now;
  struct tm timeinfo;
  while (1) {
    vTaskDelay(60 * 1000 / portTICK_PERIOD_MS);
    time(&now);
    localtime_r(&now, &timeinfo);
    ESP_LOGI("sleep", "tm_hour: %d >= %d ?", timeinfo.tm_hour,
             SLEEP_START_HOUR);
    // enter deep sleep if current hour is after SLEEP_START_HOUR; if
    // SLEEP_END_HOUR > SLEEP_START_HOUR (END is at the same day as START) then
    // current hour needs to be before SLEEP_END_HOUR to enter sleep
    if (timeinfo.tm_hour >= SLEEP_START_HOUR &&
        (SLEEP_END_HOUR <= SLEEP_START_HOUR ||
         timeinfo.tm_hour < SLEEP_END_HOUR)) {
      // correct the sleep time by time elapsed since SLEEP_START_HOUR (can
      // happen if device boots inbetween the SLEEP_START/SLEPP_END)
      uint8_t hoursToSleep = (SLEEP_END_HOUR < SLEEP_START_HOUR)
                                 ? (24 - timeinfo.tm_hour) + SLEEP_END_HOUR
                                 : SLEEP_END_HOUR - timeinfo.tm_hour;
      uint64_t usecToSleepFromNow = H_TO_USEC(hoursToSleep) -
                                    M_TO_USEC(timeinfo.tm_min) -
                                    S_TO_USEC(timeinfo.tm_sec);
      // ESP_LOGI("sleep", "tm_hour: %d, tm_min: %d, tm_sec: %d, hoursToSleep:
      // %d",
      //          timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
      //          hoursToSleep);
      // ESP_LOGI("sleep", "tm_min: %lld, tm_sec: %lld, hoursToSleep: %lld",
      //          M_TO_USEC(timeinfo.tm_min), S_TO_USEC(timeinfo.tm_sec),
      //          H_TO_USEC(hoursToSleep));
      // ESP_LOGI("SLEEP", "Entering deep sleep for %lld usec",
      //          usecToSleepFromNow);
      esp_deep_sleep(usecToSleepFromNow);
    }
  }
}

void on_switchbot_data_update(SwitchBot *sb) {
  ESP_LOGI(tag, "SwitchBot update: %s", sb->role_name());
  SWITCHBOT_BAT = sb->battery();
  SWITCHBOT_UPDATE = true;
  return;
}
SwitchBot::on_update_fn sb_update_fn = on_switchbot_data_update;
BLDeviceController gDeviceController(new SwitchBot(sb_update_fn),
                                     new ShutterButton());

static void main_task(void *param) {
  ESP_LOGI(tag, "BLE Main Task Started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

void on_ntp_sync(struct timeval *tv) {
  time_t t;
  struct tm timeinfo;
  time(&t);
  localtime_r(&t, &timeinfo);
  char strftime_buf[64];
  strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
  ESP_LOGE(tag, "Time synced: %s", strftime_buf);
  NTP_SYNCED = true;
}

static void on_wifi_rssi_update(int8_t rssi) { WIFI_RSSI = rssi; }

static void on_wifi_disconnected(uint8_t reason, int8_t rssi) {
  WIFI_CONNECTED = false;
}

static void on_wifi_connected(esp_netif_t *netif, esp_ip4_addr_t ipv4) {
  WIFI_CONNECTED = true;
  NTP_SYNCED = false;
  WIFI_IPV4 = ipv4;
  ESP_LOGI(tag, "Wifi connected! Init NTP...");
  // set the operating mode once before client runs, else internal assert will
  // fail: assert failed: sntp_setoperatingmode
  // (Operating mode must not be set while SNTP client is running)
  if (esp_sntp_getoperatingmode() != ESP_SNTP_OPMODE_POLL) {
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  }
  esp_sntp_setservername(0, "pool.ntp.org");
  // initial sync as soon as possible
  esp_sntp_set_sync_interval(1000);
  esp_sntp_set_time_sync_notification_cb(on_ntp_sync);
  esp_sntp_init();
  // monitor initial time sync to see possible ntp errors
  // after connecting to internet
  static const uint8_t MAX_RETRIES = 30;
  uint8_t retry_counter = MAX_RETRIES;
  while (esp_sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED &&
         --retry_counter > 0) {
    ESP_LOGE(tag,
             "waiting 1s for NTP sync completion. Status: %d. Retries: %d/%d",
             esp_sntp_get_sync_status(), retry_counter, MAX_RETRIES);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
  // no retries left ->  timeout
  if (retry_counter == 0) {
    ESP_LOGE(tag, "NTP sync timeout.");
    return;
  }
  // sync each 30min after initial sync
  esp_sntp_set_sync_interval(1000 * 60 * 30);
}

void sync_cb() { gDeviceController.on_sync(); }
void reset_cb(int reason) { gDeviceController.on_reset(reason); }

TaskHandle_t displayTaskHandle;

static void DisplayTask(void *params) {
  ESP_LOGI("DISPLAY", "Display p: %p", params);
  if (!params) {
    ESP_LOGE(tag, "No display set!");
    return;
  }
  Display &lcd = *(Display *)params;
  lcd.clear(0x0);
  lcd.flush();
  // 40x40
  Canvas *sbSign = lcd.createArea(lcd.width() - 270, 0, lcd.width() - 230, 40);
  if (!sbSign) {
    ESP_LOGE(tag, "sbSign canvas null");
    return;
  }
  // 80x40
  Canvas *sbBatSign =
      lcd.createArea(lcd.width() - 230, 0, lcd.width() - 150, 40);
  if (!sbBatSign) {
    ESP_LOGE(tag, "sbBatSign canvas null");
    return;
  }
  // 40x40
  Canvas *wifiSign =
      lcd.createArea(lcd.width() - 150, 0, lcd.width() - 110, 40);
  if (!wifiSign) {
    ESP_LOGE(tag, "wifiSign canvas null");
    return;
  }
  // 110x40
  Canvas *wifiRssiSign =
      lcd.createArea(lcd.width() - 110, 0, lcd.width() - 0, 40);
  if (!wifiRssiSign) {
    ESP_LOGE(tag, "wifiRssiSign canvas null");
    return;
  }
  Canvas *mainCanvas =
      lcd.createArea(lcd.width() / 10, lcd.height() / 3, (lcd.width() / 10) * 8,
                     (lcd.height() / 3) * 2);
  if (!mainCanvas) {
    ESP_LOGE(tag, "time canvas null");
    return;
  }
  Canvas *bottomCanvas =
      lcd.createArea((lcd.width() / 16) * 3, (lcd.height() / 3 * 2) + 20,
                     (lcd.width() / 16) * 15, lcd.height());
  if (!bottomCanvas) {
    ESP_LOGE(tag, "bottom canvas null");
    return;
  }

  char strftime_buf[64];
  int8_t lastWifiRSSI = 0;
  bool lastWifi = !WIFI_CONNECTED, lastNTP = !NTP_SYNCED;
  uint8_t timeSecs = 0;
  while (1) {
    // switchbot update
    if (SWITCHBOT_UPDATE) {
      SWITCHBOT_UPDATE = false;
      sbSign->clear(0x0);
      sbSign->drawString("S", 1, 4, 4, 4, 0xe007);
      char bat[5]; // 100% + 0-byte
      int8_t len = snprintf(bat, 5, "%d%%", SWITCHBOT_BAT);
      if (len > 0) {
        sbBatSign->clear(0x0);
        sbBatSign->drawString(bat, len, 4, 4, 3, 0xe007);
      }
    }
    // ntp sync happened or a new minute -> update time
    if (lastNTP != NTP_SYNCED || timeSecs >= 60) {
      NTP_SYNCED = false; // reset
      lastNTP = false;
      timeSecs = 0;
      mainCanvas->clear(0x0);
      time_t now;
      struct tm timeinfo;
      time(&now);
      localtime_r(&now, &timeinfo);
      uint8_t lenTimestr = (uint8_t)strftime(strftime_buf, sizeof(strftime_buf),
                                             "%R", &timeinfo);
      mainCanvas->drawString(strftime_buf, lenTimestr, 50, 15, 4, 0xff);
    }
    // wifi updates
    if (lastWifiRSSI != WIFI_RSSI) {
      lastWifiRSSI = WIFI_RSSI;
      char rssiStr[5];
      int8_t len = snprintf(rssiStr, 5, "%d", WIFI_RSSI);
      if (len > 0) {
        wifiRssiSign->clear(0x0);
        if (wifiRssiSign->drawString(rssiStr, len, 4, 4, 3,
                                     WIFI_CONNECTED ? 0xe007 : 0x00f8) !=
            ESP_OK) {
          ESP_LOGE(
              tag,
              "error: could not draw wifi rssi string: %s len: %d rssi: %d",
              rssiStr, len, WIFI_RSSI);
        }
      } else {
        ESP_LOGE(tag, "error: wifi_rssi_update: rssi: %d", WIFI_RSSI);
      }
    }
    if (lastWifi != WIFI_CONNECTED) {
      lastWifi = WIFI_CONNECTED;
      wifiSign->clear(0x0);
      wifiSign->drawString("W", 1, 4, 4, 4, WIFI_CONNECTED ? 0xe007 : 0x00f8);
      bottomCanvas->clear(0x0);
      if (WIFI_CONNECTED) {
        char ipv4[16];
        int8_t len = snprintf(ipv4, 16, IPSTR, IP2STR(&WIFI_IPV4));
        if (len > 0) {
          bottomCanvas->drawString(ipv4, len, 10, 0, 2, 0xffff);
        }
      }
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    timeSecs += 1;
  }
  delete bottomCanvas;
  delete mainCanvas;
  delete wifiRssiSign;
  delete wifiSign;
  delete sbBatSign;
  delete sbSign;
  vTaskDelete(NULL);
}

extern "C" void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // init LCD
  Display &lcd = Display::instance(Display::LANDSCAPE, 1);
  ESP_LOGI(tag, "Display: %p", &lcd);
  xTaskCreate(DisplayTask, "DISPLAYTASK", 4096, &lcd, tskIDLE_PRIORITY,
              &displayTaskHandle);

  ret = nimble_port_init();
  if (ret != ESP_OK) {
    ESP_LOGE(tag, "Failed to init nimble %d ", ret);
    return;
  }

  ble_hs_cfg.reset_cb = reset_cb;
  ble_hs_cfg.sync_cb = sync_cb;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  // init sntp. NTP service starts in on_wifi_connected
  esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
  config.start = false;
  config.smooth_sync = false;
  //  ESP_ERROR_CHECK(esp_netif_sntp_init(&config));

#ifdef WIFI_ENABLED
  // init wifi
  wifi_init_sta();
  init_http_server();
  on_wifi_connected_fn = on_wifi_connected;
  on_wifi_disconnected_fn = on_wifi_disconnected;
  on_wifi_rssi_fn = on_wifi_rssi_update;
#endif // WIFI_ENABLED

  nimble_port_freertos_init(main_task);

#ifdef DEEP_SLEEP_ENABLED
  // start sleep task
  xTaskCreate(sleep_task, "SLEEP", 8192, NULL, tskIDLE_PRIORITY, NULL);
#endif // DEEP_SLEEP_ENABLED

  return;
}
