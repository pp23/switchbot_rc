#ifndef _SWITCHBOT_NET_H_
#define _SWITCHBOT_NET_H_

#include <cstddef>
#include <stdint.h>

#include "esp_netif.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

#define WIFI_SSID CONFIG_WIFI_SSID         // passed in on build via env vars
#define WIFI_PASSWORD CONFIG_WIFI_PASSWORD // passed in on build via env vars

#define SCRATCH_BUFSIZE (10240)

typedef struct rest_server_context {
  // char base_path[ESP_VFS_PATH_MAX + 1];
  char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

typedef void (*on_connected_fn)(esp_netif_t *netif);
typedef void (*on_disconnected_fn)(uint8_t reason, int8_t rssi);
typedef void (*on_rssi_fn)(int8_t rssi);

static const uint8_t MAX_WIFI_CONNECT_RETRIES = 3;
static const uint8_t WIFI_CONNECT_RETRY_DELAY_SEC = 30;

void wifi_init_sta();
void init_http_server();

inline on_connected_fn on_wifi_connected_fn = NULL;
inline on_disconnected_fn on_wifi_disconnected_fn = NULL;
inline on_rssi_fn on_wifi_rssi_fn = NULL;

#endif
