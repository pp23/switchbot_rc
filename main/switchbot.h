#ifndef SWITCHBOT_H
#define SWITCHBOT_H

#include "device/device_service.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_buffer.h"
#include "freertos/idf_additions.h"
#include "freertos/projdefs.h"
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
#include "portmacro.h"
#include "services/gap/ble_svc_gap.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdio.h>

#include "device/device_connection.h"
#include "device/device_role.h"
#include "device/rx_event.h"
#include "device/ble_device_role.h"


class SwitchBot : public BLEDeviceRole {
public:
  //! Checks the advertisement data for manufacturer ID of switchbot 0x0969
  //! (littleendian) Woan Technology
  IBLEDeviceRole *find_role(const struct ble_hs_adv_fields &advFields) {
    return advFields.mfg_data_len > 2 && advFields.mfg_data[0] == 0x69 &&
                   advFields.mfg_data[1] == 0x09
               ? this
               : NULL;
  }

  const char *role_name() const { return "SWITCHBOT"; }

  typedef void (*on_update_fn)(SwitchBot *);
  typedef uint8_t CommandIndex;
  enum class Command : CommandIndex {
    NOP = 0xff,
    ON = 0,
  };

  SwitchBot() : _update(NULL) {};
  SwitchBot(on_update_fn update_fn);
  ~SwitchBot();

  static void send_command(DeviceConnection *conn, Command cmd);

protected:
  constexpr static const uint8_t commands[1][3] = {
      {0x57, 0x01, 0x01}, // ON (moves finger)
  };

  //! Connect only for first time
  bool can_connect(const DeviceConnection *conn) const {
    static uint8_t CONN_COUNTER = 0;
    return CONN_COUNTER == 0 ? ++CONN_COUNTER : false;
    // TODO: refactor. Only on demand connections should be allowed. But after
    // disconnect, the connection of this role cannot be found any longer.
    // Probably because the conneciton clear() deleted a value used for finding
    // the connection.
    // return true;
  }

  void update();

  void on_data(const RxEvent *rxEvent) {
    ESP_LOGE("switchbot", "on_data: Not implemented");
  }

private:
  on_update_fn _update;
};

#endif
