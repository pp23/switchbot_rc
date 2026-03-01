#ifndef SHUTTER_BUTTON_H
#define SHUTTER_BUTTON_H

#include "device/advertising_event.h"
#include "device/ble_device_role.h"
#include "device/rx_event.h"
#include "switchbot.h"
#include <cstdint>

// bigendian as device addresses are bigendian
static const uint8_t RED_SHUTTER_BUTTON[6] = {0x7f, 0xc4, 0x44,
                                              0x12, 0xff, 0xff};
static const uint8_t BLACK_SHUTTER_BUTTON[6] = {0x43, 0x9d, 0x7a,
                                                0x12, 0xff, 0xff};

inline bool is_equal(const uint8_t a[6], const uint8_t b[6]) {
  return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3] &&
         a[4] == b[4] && a[5] == b[5];
}

class ShutterButton : public BLEDeviceRole {
public:
  IBLEDeviceRole *find_role(const struct ble_hs_adv_fields &advFields) {
    return advFields.appearance == 0x03C4 && advFields.device_addr_is_present &&
                   (is_equal(advFields.device_addr, BLACK_SHUTTER_BUTTON) ||
                    is_equal(advFields.device_addr, RED_SHUTTER_BUTTON))
               ? this
               : NULL;
  }

  const char *role_name() const { return "SHUTTER"; }

protected:
  void on_data(const RxEvent *rxEvent) {
    ESP_LOGI("button", "Data received: %d", rxEvent->buf_len);
    ESP_LOG_BUFFER_HEX("button", rxEvent->buf, rxEvent->buf_len);
    if (rxEvent->buf_len != 2) { // shutter button sends 2 bytes packets
      return;
    }
    // 0xe9: iOS button pressed 0xea: android button pressed
    if ((rxEvent->buf[0] == 0xe9 || rxEvent->buf[0] == 0xea) &&
        rxEvent->buf[1] == 0x00) {
      // trigger switchbot
      DeviceConnection *sbconn = core()->find(SwitchBot().role_name());
      if (sbconn) {
        SwitchBot::send_command(sbconn, SwitchBot::Command::ON);
      }
    }
    if (rxEvent->buf[0] == 0x00 &&
        rxEvent->buf[1] == 0x00) { // button released; ignore
      return;
    }
  }

  void on_advertising(const AdvertisingEvent *advEvent) {
    ESP_LOGE("button", "Not implemented yet");
  }
};
#endif
