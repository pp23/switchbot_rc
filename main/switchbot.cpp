#include "switchbot.h"
#include "device/device_connection.h"
#include "nimble/ble.h"

class SwitchBotData {
public:
  SwitchBotData(SwitchBot *sb) : _sb(sb) {}
  void set_conn_addr(ble_addr_t conn_addr) { _conn_addr = conn_addr; }
  ble_addr_t *conn_addr() { return &_conn_addr; }
  DeviceConnection &device_connection() { return _device_connection; }
  void update() { _sb->update(); }

private:
  SwitchBot *_sb;
  ble_addr_t _conn_addr;
  DeviceConnection _device_connection;
};

static const char *tag = "switchbot_controller";
static uint8_t s_current_phy;

static int on_gap_event(struct ble_gap_event *event, void *arg);

//! Checks the advertisement data for manufacturer ID of switchbot 0x0969
//! (littleendian) Woan Technology
static bool is_switchbot(const struct ble_hs_adv_fields &advFields) {
  return advFields.mfg_data_len > 2 && advFields.mfg_data[0] == 0x69 &&
         advFields.mfg_data[1] == 0x09;
}

SwitchBot::SwitchBot(on_update_fn update_fn)
    : _data(new SwitchBotData(this)), _update(update_fn) {}

SwitchBot::~SwitchBot() { delete _data; }

void SwitchBot::update() {
  if (_update) {
    _update(this);
  }
}

void SwitchBot::send_command(CommandIndex cmd) {
  const uint8_t *cmdData = commands[cmd];
  DeviceService *mainService = _data->device_connection().mainService;
  if (!mainService) {
    return;
  }
  uint16_t chrValHandle = mainService->characteristics[0].val_handle;
  ble_gattc_write_no_rsp(_data->device_connection().conn_handle, chrValHandle,
                         ble_hs_mbuf_from_flat(cmdData, 3));
}

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

static esp_err_t connect(const ble_addr_t &addr, SwitchBotData *sbd) {
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
  memcpy(sbd->conn_addr(), &addr, sizeof(ble_addr_t)); // save for later use
  if ((ret = ble_gap_connect(own_addr_type, &addr, 30000, NULL, on_gap_event,
                             (void *)sbd)) != ESP_OK) {
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
  // ESP_LOGI(tag, "Sending...");
  // const uint8_t on[] = {0x57, 0x01, 0x01};
  // ble_gattc_write_no_rsp(conn_handle, chr->val_handle,
  //                       ble_hs_mbuf_from_flat(on, 3));
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
  SwitchBotData *sbd = (SwitchBotData *)arg;
  ESP_LOGI(tag, "Service discovered: uuid.type: %d error: %d",
           service->uuid.u.type, error->status);

  if (error && error->status != 0) {
    ESP_LOGE(tag, "Error service discovery: error.status: %d", error->status);
    return error->status;
  }
  auto cached_svc = sbd->device_connection().add_service(service);
  if (!cached_svc) {
    ESP_LOGE(tag, "Could not add new service to cache!");
  }

  switch (service->uuid.u.type) {
  case 16: {
    ESP_LOGI(tag, "Service-UUID16: %042x", service->uuid.u16.value);
    break;
  }
  case 128: {
    // consider a 128bit UUID as main purpose service
    sbd->device_connection().mainService = cached_svc;
    ESP_LOGI(tag, "Service-UUID128:");
    log_uuid128(service->uuid.u128.value);
    ble_gattc_disc_all_chrs(sbd->device_connection().conn_handle,
                            service->start_handle, service->end_handle,
                            on_chr_disc_event, (void *)cached_svc);
    break;
  }
  default:
    ESP_LOGE(tag, "Unknown Service UUI Type: %d", service->uuid.u.type);
  }
  return error->status;
}

int on_gap_event(struct ble_gap_event *event, void *arg) {
  SwitchBotData *sbd = (SwitchBotData *)arg;
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
      ESP_ERROR_CHECK(connect(event->ext_disc.addr, sbd));
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
      sbd->device_connection().conn_handle = event->link_estab.conn_handle;
      ble_gattc_disc_all_svcs(event->link_estab.conn_handle, on_svc_disc_event,
                              (void *)sbd);
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

void SwitchBot::on_reset(int reason) { ESP_LOGE(tag, "Reset: %d ", reason); }
void SwitchBot::on_sync(void) {
  esp_err_t ret;
  uint8_t allPhy;

  ESP_LOGI(tag, "Syncing.");

  // ensure proper identity address
  ret = ble_hs_util_ensure_addr(0);
  ESP_ERROR_CHECK(ret);

  s_current_phy = BLE_HCI_LE_PHY_1M_PREF_MASK;

  allPhy = BLE_HCI_LE_PHY_1M_PREF_MASK | BLE_HCI_LE_PHY_2M_PREF_MASK |
           BLE_HCI_LE_PHY_CODED_PREF_MASK;
  set_default_le_phy(allPhy, allPhy);

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
  ret = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params, on_gap_event,
                     (void *)&_data);
  ESP_ERROR_CHECK(ret);
}
