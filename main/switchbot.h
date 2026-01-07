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

class Event {
public:
  static const size_t BUF_SIZE = 128;
  const DeviceConnection *conn;
  uint8_t buf[BUF_SIZE];
  size_t buf_len = 0;

  Event(const DeviceConnection *conn) : conn(conn) {}
};

class RxEvent : public Event {
public:
  RxEvent(struct ble_gap_event *event, const DeviceConnection *conn)
      : Event(conn) {
    if (!event) {
      return;
    }
    buf_len = OS_MBUF_PKTLEN(event->notify_rx.om);
    if (buf_len > BUF_SIZE) {
      ESP_LOGE("RxEvent",
               "Rx data size bigger than buffer size: %d bytes received, "
               "buffer size is %d bytes.",
               buf_len, BUF_SIZE);
      return;
    }
    if (os_mbuf_copydata(event->notify_rx.om, 0, buf_len, (void *)&buf[0]) !=
        ESP_OK) {
      ESP_LOGE("RxEvent", "Error: Could not copy rx data!");
      return;
    }
  }
  ~RxEvent() {}
};

class SwitchBotData;

class SwitchBot : public BLEDeviceRole {
  friend class SwitchBotData;

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

  void on_reset(int reason);
  void on_sync(void);
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
  SwitchBotData *_data;
  on_update_fn _update;
};

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
};

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

  constexpr IBLEDeviceRole *
  find_role(const struct ble_hs_adv_fields &advFields);

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

#endif
