// #include "bt/host/nimble/esp-hci/include/esp_nimble_hci.h"
#include "LVGL_Driver.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_netif_types.h"
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
#include <sys/socket.h>
#include <sys/types.h>

#include "device/ble_device_controller.h"
#include "lcd/st7789.h"
#include "portmacro.h"
#include "shutterbutton.h"
#include "switchbot.h"
#include "switchbot_net.h"

static const char *tag = "main";
void on_switchbot_data_update(SwitchBot *sb) {
  ESP_LOGI(tag, "SwitchBot update: %s", sb->role_name());
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
}

static void on_wifi_connected(esp_netif_t *netif) {
  ESP_LOGI(tag, "Wifi connected! Init NTP...");
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
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

extern "C" void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

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

  // init LCD
  LCD_Init();
  BK_Light(100);
  // LVGL_Init();

  nimble_port_freertos_init(main_task);

  return;
}
