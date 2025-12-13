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

class DeviceService {
public:
  static const uint8_t MAX_CHARACTERISTICS = 16;
  ble_gatt_svc service;
  ble_gatt_chr characteristics[MAX_CHARACTERISTICS];

  operator const ble_gatt_svc *() const { return &(this->service); }
  operator ble_gatt_svc() const { return this->service; }
  ble_gatt_chr *add_characteristic(const ble_gatt_chr *chr) {
    if (!chr) {
      return NULL;
    }
    if (chr_counter >= MAX_CHARACTERISTICS) {
      return NULL;
    }
    return (ble_gatt_chr *)memcpy(&characteristics[chr_counter++], chr,
                                  sizeof(ble_gatt_chr));
  }

private:
  uint8_t chr_counter = 0; //! current characteristics count
};

class DeviceConnection {
public:
  static const uint8_t MAX_SERVICES = 16;
  uint16_t conn_handle;
  DeviceService services[MAX_SERVICES];

  //! Adds a ble service to the cache and returns a pointer to the cached
  //! service on success, otherwise NULL
  DeviceService *add_service(const ble_gatt_svc *svc) {
    if (!svc) {
      return NULL;
    }
    if (svc_counter >= MAX_SERVICES) {
      return NULL;
    }
    DeviceService *deviceService = &services[svc_counter];
    ++svc_counter;
    memcpy(&(deviceService->service), svc, sizeof(ble_gatt_svc));
    return deviceService;
  }

  uint8_t count() const { return svc_counter; }

private:
  uint8_t svc_counter = 0;
};

static const char *SWITCHBOT_PEER_ADDR = "";
static const char *tag = "switchbot_controller";
static uint8_t s_current_phy;
static ble_addr_t conn_addr;
static DeviceConnection services;

static int on_gap_event(struct ble_gap_event *event, void *arg);

void set_default_le_phy(uint8_t tx_phys_mask, uint8_t rx_phys_mask) {
  if (ble_gap_set_prefered_default_le_phy(tx_phys_mask, rx_phys_mask) == 0) {
    ESP_LOGI(tag, "Default LE PHY set successfully; tx_phy = %d, rx_phy = %d",
             tx_phys_mask, rx_phys_mask);
  } else {
    ESP_LOGE(tag, "Failed to set default LE PHY");
  }
}

static esp_err_t parse_adv_data(struct ble_hs_adv_fields *advFields,
                                const uint8_t *data, uint8_t length) {
  esp_err_t ret;
  if ((ret = ble_hs_adv_parse_fields(advFields, data, length)) != 0) {
    ESP_LOGE(tag, "Could not parse advertising fields: %d", ret);
  }
  return ret;
}

static bool is_switchbot(const struct ble_hs_adv_fields &advFields) {
  return advFields.mfg_data_len > 2 && advFields.mfg_data[0] == 0x69 &&
         advFields.mfg_data[1] ==
             0x09; // manufacturer id little endian 0x0969 == Woan Technology
}

static void ext_print_adv_report(const ble_gap_ext_disc_desc *disc) {
  ESP_LOGI(tag, "props=%d data_status=%d legacy_event_type=%d", disc->props,
           disc->data_status, disc->legacy_event_type);
  ESP_LOGI(tag, "address=%02x:%02x:%02x:%02x:%02x:%02x", disc->addr.val[5],
           disc->addr.val[4], disc->addr.val[3], disc->addr.val[2],
           disc->addr.val[1], disc->addr.val[0]);
  ESP_LOGI(tag, "direct_address=%02x:%02x:%02x:%02x:%02x:%02x",
           disc->direct_addr.val[5], disc->direct_addr.val[4],
           disc->direct_addr.val[3], disc->direct_addr.val[2],
           disc->direct_addr.val[1], disc->direct_addr.val[0]);
  ESP_LOGI(tag, "rssi=%d tx_power=%d", disc->rssi, disc->tx_power);
  ESP_LOGI(tag, "sid=%d prim_phy=%d sec_phy=%d", disc->sid, disc->prim_phy,
           disc->sec_phy);
  ESP_LOGI(tag, "periodic_adv_itvl=%d length_data=%d", disc->periodic_adv_itvl,
           disc->length_data);
  ESP_LOG_BUFFER_HEX(tag, disc->data, disc->length_data);
}

//! out should have at least size of 19
static const char *addr_to_string(char *out, const ble_addr_t &addr) {
  sprintf(out, "%02x:%02x:%02x:%02x:%02x:%02x", addr.val[5], addr.val[4],
          addr.val[3], addr.val[2], addr.val[1], addr.val[0]);
  return out;
}

static esp_err_t connect(const ble_addr_t &addr) {
  esp_err_t ret = 0;
  uint8_t own_addr_type;
#if !(MYNEWT_VAL(BLE_HOST_ALLOW_CONNECT_WITH_SCAN))
  /* Scanning must be stopped before a connection can be initiated. */
  if ((ret = ble_gap_disc_cancel()) != ESP_OK) {
    ESP_LOGE(tag, "Failed to cancel scan; ret=%d\n", ret);
    return ret;
  }
#endif
  if ((ret = ble_hs_id_infer_auto(0, &own_addr_type)) != ESP_OK) {
    ESP_LOGE(tag, "Could not infer own address type");
    return ret;
  }
  memcpy(&conn_addr, &addr, sizeof(ble_addr_t)); // save for later use
  if ((ret = ble_gap_connect(own_addr_type, &addr, 30000, NULL, on_gap_event,
                             (void *)&conn_addr)) != ESP_OK) {
    char out[19];
    ESP_LOGE(tag, "Could not connect to %s: %d", addr_to_string(out, addr),
             ret);
    return ret;
  }
  return ret;
}

static void log_uuid128(const uint8_t value[16]) {
  const uint8_t *uuid = &value[0];
  // TODO: little endian or big endian?
  ESP_LOGI(
      tag,
      "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
      uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
      uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14],
      uuid[15]);
}

// int ble_gatt_chr_fn(uint16_t conn_handle,
//                            const struct ble_gatt_error *error,
//                             const struct ble_gatt_chr *chr, void *arg);
static int on_chr_disc_event(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             const struct ble_gatt_chr *chr, void *arg) {
  if (!chr) {
    if (error) {
      return error->status;
    }
    return 1;
  }
  ESP_LOGI(tag, "Characteristics discovered: uuid.type: %d val_handle: %d",
           chr->uuid.u.type, chr->val_handle);
  if (error && error->status != ESP_OK) {
    ESP_LOGE(tag, "Error discovering characteristic: error.status: %d",
             error->status);
    return error->status;
  }
  DeviceService *deviceService = (DeviceService *)arg;
  if (!deviceService) {
    ESP_LOGE(tag, "No DeviceService argument provided to characteristic "
                  "discovery callback");
  } else {
    deviceService->add_characteristic(chr);
  }
  switch (chr->uuid.u.type) {
  case 16: {
    ESP_LOGI(tag, "Characteristics-UUID16: %042x", chr->uuid.u16.value);
    break;
  }
  case 128: {
    ESP_LOGI(tag, "Characteristics-UUID128:");
    log_uuid128(chr->uuid.u128.value);
    break;
  }
  default:
    ESP_LOGE(tag, "Unknown Characteristics UUID type: %d", chr->uuid.u.type);
    return 1;
  }
  ESP_LOGI(tag, "Sending...");
  const uint8_t press[] = {0x57, 0x01};
  const uint8_t on[] = {0x57, 0x01, 0x01};
  ble_gattc_write_no_rsp(conn_handle, chr->val_handle,
                         ble_hs_mbuf_from_flat(on, 3));
  return 0;
}

// int ble_gatt_disc_svc_fn(uint16_t conn_handle,
//                                 const struct ble_gatt_error *error,
//                                 const struct ble_gatt_svc *service,
//                                 void *arg);
static int on_svc_disc_event(uint16_t conn_handle,
                             const struct ble_gatt_error *error,
                             const struct ble_gatt_svc *service, void *arg) {
  if (service == NULL) {
    if (error != NULL) {
      return error->status;
    }
    return 1;
  }
  ESP_LOGI(tag, "Service discovered: uuid.type: %d error: %d",
           service->uuid.u.type, error->status);

  if (error && error->status != 0) {
    ESP_LOGE(tag, "Error service discovery: error.status: %d", error->status);
    return error->status;
  }
  auto cached_svc = services.add_service(service);
  if (!cached_svc) {
    ESP_LOGE(tag, "Could not add new service to cache!");
  }

  switch (service->uuid.u.type) {
  case 16: {
    ESP_LOGI(tag, "Service-UUID16: %042x", service->uuid.u16.value);
    break;
  }
  case 128: {
    ESP_LOGI(tag, "Service-UUID128:");
    log_uuid128(service->uuid.u128.value);
    ble_gattc_disc_all_chrs(services.conn_handle, service->start_handle,
                            service->end_handle, on_chr_disc_event,
                            (void *)cached_svc);
    break;
  }
  default:
    ESP_LOGE(tag, "Unknown Service UUI Type: %d", service->uuid.u.type);
  }
  return error->status;
}

static int on_gap_event(struct ble_gap_event *event, void *arg) {
  ESP_LOGI(tag, "BLE Event received: %d", event->type);
  switch (event->type) {
  case BLE_GAP_EVENT_EXT_DISC: { // 19
    ext_print_adv_report(&event->ext_disc);
    struct ble_hs_adv_fields advFields;
    parse_adv_data(&advFields, event->ext_disc.data,
                   event->ext_disc.length_data);
    if (is_switchbot(advFields)) {
      ESP_LOGI(tag, "AdvertisingFields:");
      ESP_LOGI(tag, "mfg:");
      ESP_LOG_BUFFER_HEX(tag, advFields.mfg_data, advFields.mfg_data_len);
      char buf[19];
      ESP_LOGI(tag, "Attempt to connect to %s",
               addr_to_string(buf, event->ext_disc.addr));
      ESP_ERROR_CHECK(connect(event->ext_disc.addr));
    }
    break;
  }
  case BLE_GAP_EVENT_CONNECT: {
    const ble_addr_t *addr = (const ble_addr_t *)arg;
    ESP_LOGI(tag, "Connected to %02x:%02x:%02x:%02x:%02x:%02x with type %d",
             addr->val[5], addr->val[4], addr->val[3], addr->val[2],
             addr->val[1], addr->val[0], addr->type);
    break;
  }
  case BLE_GAP_EVENT_DATA_LEN_CHG: { // 34
    const struct ble_hci_ev_le_subev_data_len_chg *dl_chg =
        (struct ble_hci_ev_le_subev_data_len_chg *)arg;
    ESP_LOGI(tag, "DATA_LEN_CHANGE: max_rx_time: %d", dl_chg->max_rx_time);
    break;
  }
  case BLE_GAP_EVENT_LINK_ESTAB: { // 38
    if (event->link_estab.status == ESP_OK) {
      ESP_LOGI(tag, "Link established!");
      services.conn_handle = event->link_estab.conn_handle;
      ble_gattc_disc_all_svcs(event->link_estab.conn_handle, on_svc_disc_event,
                              NULL);
    } else {
      ESP_LOGE(tag, "Could not establish link to switchbot: 0x%02x",
               event->link_estab.status);
    }
    break;
  }
  case BLE_GAP_EVENT_DISCONNECT: { // 1
    // reasons:
    // https://mynewt.apache.org/master/network/ble_hs/ble_hs_return_codes.html
    ESP_LOGI(tag, "Disconnect. reason=0x%02x", event->disconnect.reason);
    break;
  }
  default:
    ESP_LOGE(tag, "Unkwnonw BLE event: %d", event->type);
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
