#ifndef BLE_DEVICE_CONTROLLER_H
#define BLE_DEVICE_CONTROLLER_H

#include "ble_device_role.h"
#include "device_connection.h"

#include "esp_err.h"
#include "esp_event.h"
#include "esp_event_base.h"
#include "freertos/idf_additions.h"
#include "host/ble_gap.h"
#include "host/ble_hs_adv.h"
#include "portmacro.h"
#include <cstdint>

class BLDeviceController : public IBLEDeviceRole {
  friend class BLEDeviceRole;

public:
  static const uint8_t MAX_CONNECTIONS = 4;
  static const TickType_t MAX_MTX_WAIT_TICKS = 100;
  template <typename... BLDeviceRoles>
  BLDeviceController(BLDeviceRoles... roles) : _roleCount(sizeof...(roles)) {
    _accessMtx = xSemaphoreCreateBinary();
    if (_accessMtx == NULL) {
      ESP_LOGE("core", "ERROR: Coould not create access mutex!");
      return;
    }
    _roles = new BLEDeviceRole *[sizeof...(BLDeviceRoles)];
    set_roles(roles...);
    set_core(roles...);
    // Create a user event loop to not get blocked by vTaskDelays on the default
    // event loop Uses half the max prio as we do probably not need high prio
    // but also no idle prio to get ble-events and reactions in reasonable time
    UBaseType_t evtLoopPrio =
        MAX((configMAX_PRIORITIES - 1) / 2, tskIDLE_PRIORITY + 1);
    ESP_LOGI("core", "Event loop prio: %d", evtLoopPrio);
    esp_event_loop_args_t loop_args = {.queue_size = 4,
                                       .task_name = "ble_evt_loop",
                                       .task_priority = evtLoopPrio,
                                       .task_stack_size = 3072,
                                       .task_core_id = tskNO_AFFINITY};
    ESP_ERROR_CHECK(esp_event_loop_create(&loop_args, &this->_eventLoop));
    // mutex gets created in a "taken" state
    xSemaphoreGive(_accessMtx);
  };

  ~BLDeviceController() {
    delete[] _roles;
    esp_event_loop_delete(this->_eventLoop);
  }

  esp_event_loop_handle_t event_loop() const { return _eventLoop; }

  //! Return the BLEDevice if it matches the advertising fields.
  //! Thread safe as long as roles do not change during runtime.
  constexpr IBLEDeviceRole *
  find_role(const struct ble_hs_adv_fields &advFields) {
    for (uint8_t i = 0; i < _roleCount; ++i) {
      if (_roles[i]->find_role(advFields)) {
        return _roles[i];
      }
    }
    return NULL;
  }

  //! Returns a registered connection with the given conn_handle or NULL
  DeviceConnection *find(uint16_t conn_handle);

  //! Returns a registered connection with the given address or NULL
  DeviceConnection *find(const ble_addr_t &addr);

  // TODO: find all connections by type instead of the first matching one
  /*template <class T>*/
  DeviceConnection *find(const char *role_name);

  //! Returns an available device
  const DeviceConnection *get(uint8_t index) const;

  uint8_t connection_count() const {
    if (xSemaphoreTake(_accessMtx, MAX_MTX_WAIT_TICKS) != pdTRUE) {
      ESP_LOGE("core", "Could not obtain mutex!");
      return 0;
    }
    uint8_t result = _connection_count();
    xSemaphoreGive(_accessMtx);
    return result;
  }

  //! Frees a connection slot if not connected
  bool remove(DeviceConnection *conn) {
    if (xSemaphoreTake(_accessMtx, MAX_MTX_WAIT_TICKS) != pdTRUE) {
      ESP_LOGE("core", "Could not obtain mutex!");
      return 0;
    }
    // remove only if not connected
    if (!conn->connected) {
      conn->clear();
      xSemaphoreGive(_accessMtx);
      return true;
    }
    xSemaphoreGive(_accessMtx);
    return false;
  }

  const char *role_name() const { return "CORE"; }

  void on_reset(int reason);
  void on_sync();

protected:
  //! Creates a new device. Returns NULL if MAX_DEVICES reached. Can only be
  //! called by friend roles.
  DeviceConnection *create();

  bool can_connect(const DeviceConnection *conn) const { return true; }
  void on_data(const RxEvent *) {}
  void on_advertising(const AdvertisingEvent *) {}

private:
  template <typename A> constexpr void set_core(A a) { a->set_core(this); }
  template <typename A, typename... B> constexpr void set_core(A a, B... b) {
    a->set_core(this);
    this->set_core<B...>(b...);
  }
  template <typename A, int i> constexpr void set_roles(A a) { _roles[i] = a; }
  template <typename A, typename... B, int i = 0>
  constexpr void set_roles(A a, B... b) {
    _roles[i] = a;
    this->set_roles<B..., i + 1>(b...);
  }
  //! Counts used connections (not thread safe)
  uint8_t _connection_count() const {
    uint8_t result = 0;
    for (const DeviceConnection &conn : _connection_pool) {
      if (conn.in_use()) {
        ++result;
      }
    }
    return result;
  }
  BLEDeviceRole **_roles;
  const uint8_t _roleCount;
  DeviceConnection _connection_pool[MAX_CONNECTIONS];
  SemaphoreHandle_t _accessMtx = NULL;
  esp_event_loop_handle_t _eventLoop = NULL;
};

#endif // !BLE_DEVICE_CONTROLLER_H
