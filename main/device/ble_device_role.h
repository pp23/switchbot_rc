#ifndef BLE_DEVICE_ROLE_H
#define BLE_DEVICE_ROLE_H

#include "device_role.h"

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

#endif // !BLE_DEVICE_ROLE_H
