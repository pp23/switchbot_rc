// #include "bt/host/nimble/esp-hci/include/esp_nimble_hci.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_buffer.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "nvs_flash.h"

#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdio.h>

#include "device/device_connection.h"
#include "switchbot.h"

static const char* tag = "main";

static void main_task(void *param) {
  ESP_LOGI(tag, "BLE Main Task Started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

static SwitchBot switchbot;
static void sync_cb(){
    switchbot.on_sync();
}
static void reset_cb(int reason){
  switchbot.on_reset(reason);
}
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

  nimble_port_freertos_init(main_task);

  return;
}
