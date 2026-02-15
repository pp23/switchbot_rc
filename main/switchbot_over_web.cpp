// #include "bt/host/nimble/esp-hci/include/esp_nimble_hci.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
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
// #define DEEP_SLEEP_ENABLED

static const char *tag = "main";

static int8_t WIFI_RSSI = 0;
static bool WIFI_CONNECTED = false;
static bool NTP_SYNCED = false;
static bool SWITCHBOT_UPDATE = false;

static const uint8_t SLEEP_START_HOUR = 16; // start deep sleep at 16h UTC
static const uint8_t SLEEP_END_HOUR = 12;   // end deep sleep at 12h UTC
static const uint64_t SLEEP_DIFF_USEC =
    (uint64_t)(24 - ABSOLUTE(SLEEP_END_HOUR - SLEEP_START_HOUR) * (uint64_t)60 *
                        (uint64_t)60 * (uint64_t)1000 * (uint64_t)1000);

void sleep_task(void *params) {
  time_t now;
  struct tm timeinfo;
  struct timeval tv;
  while (1) {
    vTaskDelay(60 * 1000 / portTICK_PERIOD_MS);
    time(&now);
    localtime_r(&now, &timeinfo);
    if (timeinfo.tm_hour >= SLEEP_START_HOUR) {
      if (gettimeofday(&tv, NULL) != 0) {
        continue; // try again next iteration
      }
      // correct the sleep time to the full hour
      uint64_t sleep_usec = SLEEP_DIFF_USEC -
                            (timeinfo.tm_min * 60 * 1000 * 1000) -
                            (timeinfo.tm_sec * 1000 * 1000);
      ESP_LOGI("SLEEP", "Entering deep sleep...");
      esp_deep_sleep(sleep_usec);
    }
  }
}

void on_switchbot_data_update(SwitchBot *sb) {
  ESP_LOGI(tag, "SwitchBot update: %s", sb->role_name());
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

static void on_wifi_connected(esp_netif_t *netif) {
  WIFI_CONNECTED = true;
  NTP_SYNCED = false;
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
  char strftime_buf[64];
  int8_t lastWifiRSSI = 0;
  bool lastWifi = !WIFI_CONNECTED, lastNTP = !NTP_SYNCED;
  uint8_t timeSecs = 0;
  while (1) {
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
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    timeSecs += 1;
  }
  delete mainCanvas;
  delete wifiRssiSign;
  delete wifiSign;
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

  // init wifi
  wifi_init_sta();
  init_http_server();
  on_wifi_connected_fn = on_wifi_connected;
  on_wifi_disconnected_fn = on_wifi_disconnected;
  on_wifi_rssi_fn = on_wifi_rssi_update;

  nimble_port_freertos_init(main_task);

#ifdef DEEP_SLEEP_ENABLED
  // start sleep task
  xTaskCreate(sleep_task, "SLEEP", 4096, NULL, tskIDLE_PRIORITY, NULL);
#endif // DEEP_SLEEP_ENABLED

  return;
}
