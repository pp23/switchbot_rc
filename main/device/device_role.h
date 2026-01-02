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
  ////! Returns the unique identifier of this device
  // virtual const char *id() const = 0;
  ////! Returns the common type of device
  // virtual const char *type() const = 0;
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
  virtual ~BLEDeviceRole() {}
  //! Takes a connection from the pool. Returns it if available, else NULL
  DeviceConnection *create();

  virtual bool can_connect(const DeviceConnection *conn) const { return true; }

private:
  IBLEDeviceRole *_core = NULL;
};
#endif
