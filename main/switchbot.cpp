#include "switchbot.h"
#include "device/device_connection.h"
#include "device/device_role.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_event_base.h"
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

class SwitchBotData {
public:
  SwitchBotData() : _sb(NULL) {}
  void setSwitchBot(SwitchBot *sb) { _sb = sb; }
  void set_conn_addr(ble_addr_t conn_addr) { _conn_addr = conn_addr; }
  ble_addr_t *conn_addr() { return &_conn_addr; }
  DeviceConnection &device_connection() { return _device_connection; }
  void update() {
    if (_sb) {
      _sb->update();
    }
  }

private:
  SwitchBot *_sb;
  ble_addr_t _conn_addr;
  DeviceConnection _device_connection;
};

//! Return the BLEDevice if it matches the advertising fields.
//! Thread safe as long as roles do not change during runtime.
template <typename... BLEDevices>
constexpr IBLEDeviceRole *BLDeviceController<BLEDevices...>::find_role(
    const struct ble_hs_adv_fields &advFields) {
  for (IBLEDeviceRole *role : _roles) {
    if (role->find_role(advFields)) {
      return role;
    }
  }
  return NULL;
}

//! Returns a registered connection with the given conn_handle or NULL
template <typename... BLEDevices>
DeviceConnection *
BLDeviceController<BLEDevices...>::find(uint16_t conn_handle) {
  if (xSemaphoreTake(_accessMtx, MAX_MTX_WAIT_TICKS) != pdTRUE) {
    ESP_LOGE("core", "Could not obtain mutex!");
    return NULL;
  }
  ESP_LOGI("core", "Searching for connection with conn_handle: %d",
           conn_handle);
  for (DeviceConnection &conn : _connection_pool) {
    if (conn.conn_handle() == conn_handle) {
      xSemaphoreGive(_accessMtx);
      return &conn;
    }
  }
  xSemaphoreGive(_accessMtx);
  return NULL;
}

//! Returns a registered connection with the given address or NULL
template <typename... BLEDevices>
DeviceConnection *
BLDeviceController<BLEDevices...>::find(const ble_addr_t &addr) {
  if (xSemaphoreTake(_accessMtx, MAX_MTX_WAIT_TICKS) != pdTRUE) {
    ESP_LOGE("core", "Could not obtain mutex!");
    return NULL;
  }
  for (DeviceConnection &conn : _connection_pool) {
    if (conn.get_addr() == addr) {
      xSemaphoreGive(_accessMtx);
      return &conn;
    }
  }
  xSemaphoreGive(_accessMtx);
  return NULL;
}

template <typename... BLEDevices>
DeviceConnection *
BLDeviceController<BLEDevices...>::find(const char *role_name) {
  if (xSemaphoreTake(_accessMtx, MAX_MTX_WAIT_TICKS) != pdTRUE) {
    ESP_LOGE("core", "Could not obtain mutex!");
    return NULL;
  }
  ESP_LOGI("core", "Searching for role with role_name %s", role_name);
  for (DeviceConnection &conn : _connection_pool) {
    if (!conn.role()) {
      continue;
    }
    if (strcmp(conn.role()->role_name(), role_name) == 0) {
      xSemaphoreGive(_accessMtx);
      return &conn;
    }
  }
  xSemaphoreGive(_accessMtx);
  return NULL;
}

//! Creates a new device. Returns NULL if MAX_DEVICES reached.
template <typename... BLEDevices>
DeviceConnection *BLDeviceController<BLEDevices...>::create() {
  if (xSemaphoreTake(_accessMtx, MAX_MTX_WAIT_TICKS) != pdTRUE) {
    ESP_LOGE("core", "Could not obtain mutex!");
    return NULL;
  }
  if (_connection_count() < MAX_CONNECTIONS) {
    for (DeviceConnection &conn : _connection_pool) {
      if (!conn.in_use()) {
        conn.set_inuse(true);
        xSemaphoreGive(_accessMtx);
        return &conn;
      }
    }
  }
  xSemaphoreGive(_accessMtx);
  return NULL;
}

esp_err_t connect(const ble_addr_t &addr, DeviceConnection *dc);

struct ble_npl_event schedule_event;

static BLDeviceController gDeviceController(new SwitchBot(),
                                            new ShutterButton());
static SwitchBotData gSwitchBotData;
static DeviceConnection *sbconn = NULL;

static const char *tag = "switchbot_controller";
static uint8_t s_current_phy;

static int on_gap_event(struct ble_gap_event *event, void *arg);

static const esp_event_base_t bleEventBase = "ble_event";
static const int32_t DATA_RX_EVENT = 1;
void on_rx_data(void *args, esp_event_base_t base, int32_t id,
                void *event_data) {
  ESP_LOGI(tag, "Event scheduled");
  RxEvent event = *(RxEvent *)event_data;
  ESP_LOGI("on_rx_data", "RxEvent: %p", event_data);
  IBLEDeviceRole *role(event.conn->role());
  if (!role) {
    ESP_LOGE(tag, "ERROR: No role set for connection!");
    return;
  }
  ESP_LOGI(tag, "Calling role %s", role->role_name());
  role->on_data(&event);
  // if (!sbconn->connected) {
  //   ESP_ERROR_CHECK(connect(sbconn->get_addr(), sbconn));
  // } else {
  //   const uint8_t on[] = {0x57, 0x01, 0x01};
  //   ble_gattc_write_no_rsp(sbconn->conn_handle,
  //                          sbconn->mainService->characteristics[1].val_handle,
  //                          ble_hs_mbuf_from_flat(on, 3));
  // }
}

SwitchBot::SwitchBot(on_update_fn update_fn)
    : _data(&gSwitchBotData), _update(update_fn) {
  _data->setSwitchBot(this);
}

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

esp_err_t connect(const ble_addr_t &addr, DeviceConnection *dc) {
  esp_err_t ret = 0;
  uint8_t own_addr_type;
  if (!dc) {
    ESP_LOGE(tag, "ERROR: DeviceConnection not set!");
    return ESP_FAIL;
  }
#if !(MYNEWT_VAL(BLE_HOST_ALLOW_CONNECT_WITH_SCAN))
  ESP_LOGI(tag, "Stopping scan");
  /* Scanning must be stopped before a connection can be initiated. */
  if ((ret = ble_gap_disc_cancel()) != ESP_OK) {
    ESP_LOGE(tag, "Failed to cancel scan; ret=%d\n", ret);
    return ret;
  }
#endif
  ESP_LOGI(tag, "Trying to connect to %02x with connection %s", addr.val[5],
           dc->str().c_str());
  if ((ret = ble_hs_id_infer_auto(0, &own_addr_type)) != ESP_OK) {
    ESP_LOGE(tag, "Could not infer own address type");
    return ret;
  }
  dc->set_addr(&addr); // save for later use
  if ((ret = ble_gap_connect(own_addr_type, &addr, 30000, NULL, on_gap_event,
                             (void *)dc)) != ESP_OK) {
    char out[19];
    ESP_LOGE(tag, "Could not connect to %s: %d", addr_to_string(out, addr),
             ret);
    if (ret == BLE_HS_EALREADY) {
      ESP_LOGE(tag, "ERROR: Connection attempt already in progress");
    } else if (ret == BLE_HS_EBUSY) {
      ESP_LOGE(tag,
               "ERROR: Connecting not possible due to scanning in progress");
    } else if (ret == BLE_HS_EDONE) {
      ESP_LOGE(tag, "ERROR: Already connected");
    }
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
  DeviceConnection *dc = (DeviceConnection *)arg;
  if (!dc) {
    ESP_LOGE(tag, "ERROR: Passed argument is null");
    return 1;
  }
  ESP_LOGI(tag, "Service discovered: uuid.type: %d error: %d",
           service->uuid.u.type, error->status);

  if (error && error->status != 0) {
    ESP_LOGE(tag, "Error service discovery: error.status: %d", error->status);
    return error->status;
  }
  auto cached_svc = dc->add_service(service);
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
    // consider a 128bit UUID as main purpose service
    dc->set_main_service(cached_svc);
    ble_gattc_disc_all_chrs(dc->conn_handle(), service->start_handle,
                            service->end_handle, on_chr_disc_event,
                            (void *)cached_svc);
    break;
  }
  default:
    ESP_LOGE(tag, "Unknown Service UUI Type: %d", service->uuid.u.type);
  }
  return error->status;
}

int on_gap_event(struct ble_gap_event *event, void *arg) {
  ESP_LOGI(tag, "BLE Event received: %d", event->type);
  switch (event->type) {
  case BLE_GAP_EVENT_EXT_DISC: { // 19
    ext_print_adv_report(&event->ext_disc);
    struct ble_hs_adv_fields advFields;
    parse_adv_data(&advFields, event->ext_disc.data,
                   event->ext_disc.length_data);
    // find a role that matches the discovered device
    IBLEDeviceRole *device = gDeviceController.find_role(advFields);
    ESP_LOGI(tag, "Connection count: %d", gDeviceController.connection_count());
    if (device) {
      ESP_LOGI(tag, "Device role found: %s", device->role_name());
      // do we know this device already?
      DeviceConnection *conn = gDeviceController.find(event->ext_disc.addr);
      if (conn == NULL) {
        // create a new connection if device not known yet
        conn = device->create();
        if (conn == NULL) {
          ESP_LOGE(tag, "ERROR: No connection slots left!");
          // TODO: Recycle an unused connection
          return ESP_OK;
        }
        ESP_LOGI(tag, "New connection created: %s", conn->str().c_str());
      } else {
        ESP_LOGI(tag, "address already known: %02x belongs to role %s",
                 event->ext_disc.addr.val[5], conn->role()->role_name());
      }
      ESP_LOGI(tag, "Conn pointer address: %p", conn);
      if (conn->set_discovery_data(event->ext_disc.data,
                                   event->ext_disc.length_data) == ESP_OK) {
        // notify role about discovery data update
        // TODO: schedule event in event loop
      }
      // connect only if allowed by role
      // e.g. connect always or only on specific conditions
      if (device->can_connect(conn)) {
        ESP_ERROR_CHECK(connect(event->ext_disc.addr, conn));
        ESP_LOGI(tag,
                 "Connection started for connection %s. Current connection "
                 "count: %d",
                 conn->str().c_str(), gDeviceController.connection_count());
      }
    }
    break;
  }
  case BLE_GAP_EVENT_CONNECT: {
    DeviceConnection *dc = (DeviceConnection *)arg;
    if (event->connect.status == ESP_OK) {
      dc->set_conn_handle(event->connect.conn_handle);
      dc->connected = true;
    }
    const ble_addr_t addr = dc->get_addr();
    ESP_LOGI(tag, "Connected to %02x:%02x:%02x:%02x:%02x:%02x with type %d",
             addr.val[5], addr.val[4], addr.val[3], addr.val[2], addr.val[1],
             addr.val[0], addr.type);
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
      DeviceConnection *dc = (DeviceConnection *)
          arg; // EVENT_LINK_ESTAB gets same arg as EVENT_CONNECT
      dc->set_conn_handle(event->link_estab.conn_handle);
      dc->connected = true;
      dc->set_link_established();
      ESP_LOGI(tag, "Connection established. Current connection count: %d",
               gDeviceController.connection_count());
      // discover services/characteristics once
      if (dc->refresh_required()) {
        ble_gattc_disc_all_svcs(event->link_estab.conn_handle,
                                on_svc_disc_event, (void *)dc);
      }
    } else {
      ESP_LOGE(tag, "Could not establish link to switchbot: 0x%02x",
               event->link_estab.status);
    }
    break;
  }
  case BLE_GAP_EVENT_L2CAP_UPDATE_REQ: { // 5
    ESP_LOGI(tag,
             "MinInterval: %d, MaxInterval: %d, Latency: %d, Timeout: %d, "
             "min_ce_len:%d,max_ce_len:%d",
             event->conn_update_req.peer_params->itvl_min,
             event->conn_update_req.peer_params->itvl_max,
             event->conn_update_req.peer_params->latency,
             event->conn_update_req.peer_params->supervision_timeout,
             event->conn_update_req.peer_params->min_ce_len,
             event->conn_update_req.peer_params->max_ce_len);
    return 0;
    break;
  }
  case BLE_GAP_EVENT_DISCONNECT: { // 1
    // reasons:
    // https://mynewt.apache.org/master/network/ble_hs/ble_hs_return_codes.html
    ESP_LOGI(tag, "Disconnect. reason=0x%02x", event->disconnect.reason);
    DeviceConnection *dc =
        gDeviceController.find(event->disconnect.conn.conn_handle);
    if (!dc) { // no registered connection with this conn_handle
      ESP_LOGE(tag, "WARNING: No device connection found with conn_handle %d",
               event->disconnect.conn.conn_handle);
    } else {
      dc->reset();
    }
    ESP_LOGI(tag, "Connection removed. Current connection count: %d",
             gDeviceController.connection_count());
    break;
  }
  case BLE_GAP_EVENT_NOTIFY_RX: { // 12
    ESP_LOGI(tag, "NOTIFY_RX: conn_handle: %d attr_handle: %d attr_len: %d ",
             event->notify_rx.conn_handle, event->notify_rx.attr_handle,
             OS_MBUF_PKTLEN(event->notify_rx.om));
    DeviceConnection *conn =
        gDeviceController.find(event->notify_rx.conn_handle);
    if (!conn) {
      ESP_LOGE(tag,
               "No device connection found for rx data from conn_handle %d!",
               event->notify_rx.conn_handle);
      return ESP_OK;
    }
    RxEvent evt = RxEvent(event, conn);
    ESP_LOGI(tag, "RxEvent evt: size: %d", sizeof(RxEvent));
    // notify role about data
    esp_event_post(bleEventBase, DATA_RX_EVENT, (const void *)&evt,
                   sizeof(RxEvent), 1000);
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

  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_event_handler_instance_register(bleEventBase, DATA_RX_EVENT, on_rx_data,
                                      NULL, NULL);
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
