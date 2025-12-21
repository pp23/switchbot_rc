#ifndef SWITCHBOT_H
#define SWITCHBOT_H

#include "device/device_service.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_log_buffer.h"
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
#include "services/gap/ble_svc_gap.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdio.h>

#include "device/device_connection.h"

class SwitchBotData;

class SwitchBot {
  friend class SwitchBotData;

public:
  typedef void (*on_update_fn)(SwitchBot *);
  typedef uint8_t CommandIndex;
  enum class Command : CommandIndex {
    ON = 0,
  };

  SwitchBot(on_update_fn update_fn);
  ~SwitchBot();

  void on_reset(int reason);
  void on_sync(void);
  void send_command(CommandIndex cmd);

protected:
  const uint8_t commands[1][3] = {
      {0x57, 0x01, 0x01}, // ON (moves finger)
  };

  void update();

private:
  SwitchBotData *_data;
  on_update_fn _update;
};

#endif
