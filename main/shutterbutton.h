#ifndef SHUTTER_BUTTON_H
#define SHUTTER_BUTTON_H

#include "device/advertising_event.h"
#include "device/ble_device_role.h"
#include "device/rx_event.h"
#include "switchbot.h"

class ShutterButton : public BLEDeviceRole {
public:
  IBLEDeviceRole *find_role(const struct ble_hs_adv_fields &advFields) {
    return advFields.appearance == 0x03C4 ? this : NULL;
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
