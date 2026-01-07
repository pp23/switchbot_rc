#ifndef DEVICECONNECTION_H
#define DEVICECONNECTION_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/_intsup.h>

#include "device_service.h"
#include "esp_err.h"
#include "esp_log.h"
#include "host/ble_gatt.h"
#include "host/ble_hs_adv.h"
#include "nimble/ble.h"

#include "device_role.h"

static bool operator==(const ble_addr_t &a, const ble_addr_t &b) {
  return a.type == b.type && memcmp((const void *)a.val, (const void *)b.val,
                                    sizeof(uint8_t) * 6) == 0;
}

static uint8_t CONNECTION_COUNTER = 0;

class StringBuf {
public:
  template <typename... S> StringBuf(S... s) : buf(new char[len(s...) + 1]) {
    cat(&buf[0], s...);
    buf[len(s...)] = '\0';
  }
  ~StringBuf() { delete[] buf; }
  operator const char *() const { return buf; }
  const char *c_str() const { return buf; }

private:
  char *buf;

  template <typename A, typename... B> size_t len(A a, B... b) {
    return strlen(a) + len(b...);
  }
  template <typename A> size_t len(A a) { return strlen(a); }

  template <typename A, typename... B> char *_cat(char *buf, A a, B... b) {
    return _cat(strcat(buf, a), b...);
  }
  // template <typename A, typename... B>
  // char *_cat(char *buf, unsigned char a, B... b) {
  //   switch (a) {
  //   case 0:
  //     strcat(buf, "0");
  //     break;
  //   case 1:
  //     strcat(buf, "1");
  //     break;
  //   case 2:
  //     strcat(buf, "2");
  //     break;
  //     // TODO
  //   }
  //
  //   return _cat(buf, b...);
  // }
  template <typename A, typename... B> char *cat(char *buf, A a, B... b) {
    return _cat(strcpy(buf, a), b...);
  }
  template <typename A> char *cat(char *buf, A a) { return strcpy(buf, a); }
  template <typename A> char *_cat(char *buf, A a) { return strcat(buf, a); }
};

class DeviceConnection {
public:
  static const uint8_t MAX_SERVICES = 16;
  static const uint8_t MAX_DISC_DATA_LEN = 128;
  static const uint16_t INVALID_CONN_HANDLE = 0xffff;
  uint8_t _count = 0;
  bool connected = false;
  DeviceService services[MAX_SERVICES];

  DeviceConnection() : _conn_handle(INVALID_CONN_HANDLE) {
    this->_count = ++CONNECTION_COUNTER;
    ESP_LOGI("dc", "Current global connection count: %d", CONNECTION_COUNTER);
  }

  uint8_t id() const { return this->_count; }

  //! Returns true if this connection just got created and is therefore
  //! requested by a role but might be not yet assigned to a role
  //! Represents a known device as it stays in_use=true until this connection
  //! was required to get replaced for a new device which happens usually if no
  //! connection slot was left anymore
  bool in_use() const { return inuse; }
  //! Set on creation of a new connection
  void set_inuse(bool value = true) { inuse = value; }
  void set_role(IBLEDeviceRole *role) { _role = role; }
  bool has_role() const { return _role != NULL; }
  IBLEDeviceRole *role() const { return _role; }
  bool is_connected() const { return connected; }
  void set_conn_handle(uint16_t conn_handle) {
    ESP_LOGI("dc", "New conn_handle: %d for role %s conn-id: %d", conn_handle,
             role()->role_name(), id());
    this->_conn_handle = conn_handle;
  }
  uint16_t conn_handle() const { return this->_conn_handle; }

  //! Refresh the services and characteristics if no main service is set
  bool refresh_required() const { return mainService == NULL; }

  void clear() {
    connected = false;
    set_conn_handle(INVALID_CONN_HANDLE);
    mainService = NULL;
    for (DeviceService &s : services) {
      s.clear();
    }
    svc_counter = 0;
    memset((void *)&_disc_data[0], 0, MAX_DISC_DATA_LEN);
  }

  //! Reset but keep the connect data for reconnecting
  void reset() {
    set_conn_handle(INVALID_CONN_HANDLE);
    connected = false;
    _link_established = false;
  }

  void set_link_established() { this->_link_established = true; }
  bool is_link_established() const { return this->_link_established; }

  const DeviceService *main_service() const {
    ESP_LOGI("dc", "main_service() ID: %d", id());
    return mainService;
  }
  void set_main_service(DeviceService *svc) {
    ESP_LOGI("dc", "set_main_service() ID: %d", id());
    this->mainService = svc;
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

  StringBuf str() const {
    return StringBuf(" Role: ", role()->role_name(),
                     " connected: ", (is_connected() ? "true" : "false"));
  }

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
  uint16_t _conn_handle = INVALID_CONN_HANDLE;
  bool _link_established = false;
  DeviceService *mainService = NULL; // service with a 128bit UUID considered as
                                     // devices purpose service
  bool inuse = false;
};
#endif
