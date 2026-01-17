#ifndef BLE_DEVICE_CONTROLLER_H
#define BLE_DEVICE_CONTROLLER_H

#include "ble_device_role.h"
#include "device_connection.h"

#include "host/ble_gap.h"
#include "host/ble_hs_adv.h"

// define concrete controller type to instantiate template-functions properly
// and avoid undefined reference linker errors
class SwitchBot;
class ShutterButton;
template<class... Roles>class BLDeviceController;
using BLEDeviceControllerType = BLDeviceController<SwitchBot*,ShutterButton*>;

template <typename... BLDeviceRoles>
class BLDeviceController : public IBLEDeviceRole {
  friend class BLEDeviceRole;

public:
  static const uint8_t MAX_CONNECTIONS = 4;
  static const TickType_t MAX_MTX_WAIT_TICKS = 100;
  BLDeviceController(BLDeviceRoles... roles) : _roles(roles...) {
    _accessMtx = xSemaphoreCreateBinary();
    if (_accessMtx == NULL) {
      ESP_LOGE("core", "ERROR: Coould not create access mutex!");
      return;
    }
    // mutex gets created in a "taken" state
    xSemaphoreGive(_accessMtx);
    set_core(roles...);
  };

  //! Return the BLEDevice if it matches the advertising fields.
  //! Thread safe as long as roles do not change during runtime.
  constexpr IBLEDeviceRole *
  find_role(const struct ble_hs_adv_fields &advFields){
  for (IBLEDeviceRole *role : _roles) {
    if (role->find_role(advFields)) {
      return role;
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

private:
  template <typename A> constexpr void set_core(A a) { a->set_core(this); }
  template <typename A, typename... B> constexpr void set_core(A a, B... b) {
    a->set_core(this);
    this->set_core<B...>(b...);
  }
  constexpr static size_t _roleCount = sizeof...(BLDeviceRoles);
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
  BLEDeviceRole *_roles[_roleCount];
  DeviceConnection _connection_pool[MAX_CONNECTIONS];
  SemaphoreHandle_t _accessMtx = NULL;
};


#endif // !BLE_DEVICE_CONTROLLER_H
