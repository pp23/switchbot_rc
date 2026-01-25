#ifndef _ADVERTISING_EVENT_H
#define _ADVERTISING_EVENT_H

#include "device_connection.h"
#include "event.h"

class AdvertisingEvent : public Event {
public:
  AdvertisingEvent(struct ble_hs_adv_fields advFields, DeviceConnection *conn)
      : Event(conn), _advFields(advFields) {}
  ~AdvertisingEvent() {}
  struct ble_hs_adv_fields get_advertising_fields() const { return _advFields; }

private:
  struct ble_hs_adv_fields _advFields;
};

#endif // !_ADVERTISING_EVENT_H
