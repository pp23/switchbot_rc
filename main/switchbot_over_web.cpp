// #include "bt/host/nimble/esp-hci/include/esp_nimble_hci.h"
#include "esp_err.h"
#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_hs_id.h"
#include "nimble/ble.h"
#include "nimble/hci_common.h"
#include "nvs_flash.h"

#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include <cstdint>
#include <stdio.h>

static const char *SWITCHBOT_PEER_ADDR = "";
static const char *tag = "switchbot_controller";
static uint8_t s_current_phy;
static ble_addr_t conn_addr;

void set_default_le_phy(uint8_t tx_phys_mask, uint8_t rx_phys_mask) {
  if (ble_gap_set_prefered_default_le_phy(tx_phys_mask, rx_phys_mask) == 0) {
    ESP_LOGI(tag, "Default LE PHY set successfully; tx_phy = %d, rx_phy = %d",
             tx_phys_mask, rx_phys_mask);
  } else {
    ESP_LOGE(tag, "Failed to set default LE PHY");
  }
}

static void ext_print_adv_report(const void *param) {
  const struct ble_gap_ext_disc_desc *disc =
      (struct ble_gap_ext_disc_desc *)param;
  ESP_LOGI(tag, "props=%d data_status=%d legacy_event_type=%d", disc->props,
           disc->data_status, disc->legacy_event_type);
  ESP_LOGI(tag, "address=%02x:%02x:%02x:%02x:%02x:%02x", disc->addr.val[5],
           disc->addr.val[4], disc->addr.val[3], disc->addr.val[2],
           disc->addr.val[1], disc->addr.val[0]);
  ESP_LOGI(tag, "direct_address=%02x:%02x:%02x:%02x:%02x:%02x",
           disc->direct_addr.val[5], disc->direct_addr.val[4],
           disc->direct_addr.val[3], disc->direct_addr.val[2],
           disc->direct_addr.val[1], disc->direct_addr.val[0]);
}

static int on_gap_event(struct ble_gap_event *event, void *arg) {
  ESP_LOGI(tag, "BLE Event received: %d", event->type);
  switch (event->type) {
  case BLE_GAP_EVENT_EXT_DISC: // 19
    ext_print_adv_report(&event->ext_disc);
  }
  return 0;
}

static void on_reset(int reason) { ESP_LOGE(tag, "Reset: %d ", reason); }
static void on_sync(void) {
  esp_err_t ret;
  uint8_t allPhy;
  uint8_t test_addr[6];
  uint32_t peer_addr[6];

  ESP_LOGI(tag, "Syncing.");

  // ensure proper identity address
  ret = ble_hs_util_ensure_addr(0);
  ESP_ERROR_CHECK(ret);

  s_current_phy = BLE_HCI_LE_PHY_1M_PREF_MASK;

  allPhy = BLE_HCI_LE_PHY_1M_PREF_MASK | BLE_HCI_LE_PHY_2M_PREF_MASK |
           BLE_HCI_LE_PHY_CODED_PREF_MASK;
  set_default_le_phy(allPhy, allPhy);

  if (s_current_phy != BLE_HCI_LE_PHY_1M_PREF_MASK) {
    sscanf(SWITCHBOT_PEER_ADDR, "%lx:%lx:%lx:%lx:%lx:%lx", &peer_addr[5],
           &peer_addr[4], &peer_addr[3], &peer_addr[2], &peer_addr[1],
           &peer_addr[0]);
    for (uint8_t i = 0; i < 6; ++i) {
      test_addr[i] = (uint8_t)peer_addr[i];
    }
    for (int i = 0; i < 6; ++i) {
      conn_addr.val[i] = test_addr[i];
    }
    conn_addr.type = 0;
    vTaskDelay(300);
    if (s_current_phy == BLE_HCI_LE_PHY_2M_PREF_MASK) {
      s_current_phy = BLE_HCI_LE_PHY_1M_PREF_MASK | BLE_HCI_LE_PHY_2M_PREF_MASK;
    }
    ble_gap_ext_connect(0, &conn_addr, 30000, s_current_phy, NULL, NULL, NULL,
                        on_gap_event, NULL);
  } else {
    uint8_t own_addr_type;
    struct ble_gap_disc_params disc_params;
    ret = ble_hs_id_infer_auto(0, &own_addr_type);
    ESP_ERROR_CHECK(ret);
    disc_params.filter_duplicates = 1;
    disc_params.passive = 1;
    disc_params.itvl = 0;
    disc_params.window = 0;
    disc_params.filter_policy = 0;
    disc_params.limited = 0;
    ret = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params,
                       on_gap_event, NULL);
    ESP_ERROR_CHECK(ret);
  }
}
static void main_task(void *param) {
  ESP_LOGI(tag, "BLE Main Task Started");
  nimble_port_run();
  nimble_port_freertos_deinit();
}

void ble_store_config_init();

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
  ble_hs_cfg.reset_cb = on_reset;
  ble_hs_cfg.sync_cb = on_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  // ble_store_config_init();
  nimble_port_freertos_init(main_task);
  // printf("Scanning for switchbot...\n");

  return;
}
