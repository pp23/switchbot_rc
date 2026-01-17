#ifndef RX_EVENT_H
#define RX_EVENT_H

#include "event.h"

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

#endif // !RX_EVENT_H
