#ifndef DEVICESERVICE_H
#define DEVICESERVICE_H

#include <cstdint>
#include <cstring>

#include "host/ble_gatt.h"

class DeviceService {
public:
  static const uint8_t MAX_CHARACTERISTICS = 16;
  ble_gatt_svc service;
  ble_gatt_chr characteristics[MAX_CHARACTERISTICS];

  void clear() {
    chr_counter = 0;
    memset((void *)&characteristics[0], 0,
           sizeof(ble_gatt_chr) * MAX_CHARACTERISTICS);
  }

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
#endif
