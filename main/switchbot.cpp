#include "switchbot.h"
#include "device/device_connection.h"
#include "device/device_role.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_buffer.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "nimble/ble.h"
#include "os/os_mbuf.h"
#include "portmacro.h"
#include <cstddef>
#include <cstdint>
#include <sys/types.h>


esp_err_t connect(const ble_addr_t &addr, DeviceConnection *dc);

struct ble_npl_event schedule_event;



static const char *tag = "switchbot_controller";



SwitchBot::SwitchBot(on_update_fn update_fn) : _update(update_fn) {}

SwitchBot::~SwitchBot() {}

void SwitchBot::update() {
  if (_update) {
    _update(this);
  }
}

void SwitchBot::send_command(DeviceConnection *conn, Command cmd) {
  static const uint8_t MAX_TIMEOUT_COUNTER = 30;
  if (!conn->is_connected()) {
    ESP_LOGI(tag, "Connecting to SwitchBot...%s", conn->str().c_str());
    ESP_ERROR_CHECK(connect(conn->get_addr(), conn));
    // TODO: refactor. no busy waiting.
    uint8_t i = 0;
    while (!conn->is_link_established() && ++i <= MAX_TIMEOUT_COUNTER) {
      ESP_LOGI(tag, "Still connecting to SwitchBot...%d/%d", i,
               MAX_TIMEOUT_COUNTER);
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    if (i >= MAX_TIMEOUT_COUNTER) {
      ESP_LOGE(tag, "Timeout!");
      return;
    }
    ESP_LOGI(tag, "Connected.");
  }
  ESP_LOGI(tag, "Sending command %d", cmd);
  const uint8_t *cmdData = commands[(uint8_t)cmd];
  const DeviceService *mainService = conn->main_service();
  if (!mainService) {
    ESP_LOGE(tag, "No MainService set!");
    return;
  }
  uint16_t chrValHandle = mainService->characteristics[1].val_handle;
  ble_gattc_write_no_rsp(conn->conn_handle(), chrValHandle,
                         ble_hs_mbuf_from_flat(cmdData, 3));
}

