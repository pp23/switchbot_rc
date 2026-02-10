// #include "bt/host/nimble/esp-hci/include/esp_nimble_hci.h"
#include "esp_bit_defs.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "host/ble_gatt.h"
#include "nvs_flash.h"

#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>

#include "device/ble_device_controller.h"
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

  // init wifi
  wifi_init_sta();
  init_http_server();

  nimble_port_freertos_init(main_task);

  return;
}
