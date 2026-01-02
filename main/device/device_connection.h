#ifndef DEVICECONNECTION_H
#define DEVICECONNECTION_H

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "device_service.h"
#include "esp_err.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_adv.h"
#include "nimble/ble.h"

#include "device_role.h"

static bool operator==(const ble_addr_t &a, const ble_addr_t &b) {
  return a.type == b.type && memcmp((const void *)a.val, (const void *)b.val,
                                    sizeof(uint8_t) * 6) == 0;
}

class DeviceConnection {
public:
  static const uint8_t MAX_SERVICES = 16;
  static const uint8_t MAX_DISC_DATA_LEN = 128;
  bool connected = false;
  uint16_t conn_handle;
  DeviceService services[MAX_SERVICES];
  DeviceService *mainService = NULL; // service with a 128bit UUID considered as
                                     // devices purpose service

  void set_role(IBLEDeviceRole *role) { _role = role; }
  bool has_role() const { return _role != NULL; }
  IBLEDeviceRole *role() const { return _role; }
  bool is_connected() const { return connected; }

  void clear() {
    connected = false;
    conn_handle = 0;
    mainService = NULL;
  }

  esp_err_t set_discovery_data(const uint8_t *data, size_t len) {
    if (len > MAX_DISC_DATA_LEN) {
      return ESP_ERR_INVALID_SIZE;
    }
    memcpy((void *)&_disc_data[0], (const void *)data, len);
    _disc_data_len = (uint8_t)len;
    return ESP_OK;
  }

  void set_addr(const ble_addr_t *addr) {
    memcpy((void *)&conn_addr, (const void *)addr,
           sizeof(ble_addr_t)); // save for later use
  }
  ble_addr_t get_addr() const { return conn_addr; }

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
  uint8_t _disc_data[MAX_DISC_DATA_LEN];
  uint8_t _disc_data_len = 0;
  uint8_t svc_counter = 0;
  ble_addr_t conn_addr;
  IBLEDeviceRole *_role;
};
#endif
