#ifndef DEVICECONNECTION_H
#define DEVICECONNECTION_H

#include <cstdint>

#include "host/ble_gatt.h"
#include "device_service.h"

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
#endif
