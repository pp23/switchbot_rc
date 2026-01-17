#ifndef EVENT_H
#define EVENT_H

#include "device/device_connection.h"

class Event {
public:
  static const size_t BUF_SIZE = 128;
  const DeviceConnection *conn;
  uint8_t buf[BUF_SIZE];
  size_t buf_len = 0;

  Event(const DeviceConnection *conn) : conn(conn) {}
};

#endif // !EVENT_H
