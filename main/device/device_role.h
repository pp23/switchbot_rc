#ifndef BLEDEVICEROLE_H
#define BLEDEVICEROLE_H

#include "os/os_mbuf.h"
#include <cstddef>
#include <cstdint>

class DeviceConnection;
class RxEvent;

class IBLEDeviceRole {
public:
  virtual ~IBLEDeviceRole() = 0;

  //! Returns the role to which the advertising fields match
  virtual IBLEDeviceRole *
  find_role(const struct ble_hs_adv_fields &advFields) = 0;

  //! Returns the connections that belong to the role
  virtual DeviceConnection *find(const char *role_name) = 0;

  //! Returns the device role name
  virtual const char *role_name() const = 0;

  //! Creates a connection to a device of this role
  virtual DeviceConnection *create() = 0;

  //! Returns true if connection is allowed
  virtual bool can_connect(const DeviceConnection *conn) const = 0;

  //! Called on data received from a device
  virtual void on_data(const RxEvent *rxEvent) = 0;
};
inline IBLEDeviceRole::~IBLEDeviceRole() {} // satisfy the linker

class BLEDeviceRole : public IBLEDeviceRole {
public:
  void set_core(IBLEDeviceRole *core) { _core = core; }
  IBLEDeviceRole *core() const { return _core; }
  virtual ~BLEDeviceRole() {}
  //! Takes a connection from the pool. Returns it if available, else NULL
  DeviceConnection *create();

  virtual bool can_connect(const DeviceConnection *conn) const { return true; }
  virtual DeviceConnection *find(const char *role_name) {
    if (_core) {
      return _core->find(role_name);
    }
    return NULL;
  }

private:
  IBLEDeviceRole *_core = NULL;
};
#endif
