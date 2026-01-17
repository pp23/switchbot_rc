#include "device_role.h"
#include "ble_device_role.h"
#include "device_connection.h"

DeviceConnection *BLEDeviceRole::create() {
  DeviceConnection *conn = _core ? _core->create() : NULL;
  if (conn) {
    conn->set_role(this); // assign the connection to this role
    return conn;
  }
  return NULL;
}
