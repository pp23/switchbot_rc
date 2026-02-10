#include "switchbot_net.h"

#include "esp_event.h"
#include "esp_event_base.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_wifi.h"
#include "esp_wifi_types_generic.h"
#include "lwip/sockets.h"
#include "net/if.h"
#include "portmacro.h"

static uint8_t wifi_connect_retry_counter = 0;
static EventGroupHandle_t s_wifi_event_group;
static TaskHandle_t wifi_task_handle = NULL;
static const char *tag = "web";

esp_err_t root_get_handler(httpd_req_t *req) {
  int sockfd = httpd_req_to_sockfd(req);
  char ipstr[INET6_ADDRSTRLEN];
  struct sockaddr_in6 addr; // esp_http_server uses IPv6 addressing
  socklen_t addr_size = sizeof(addr);
  if (getpeername(sockfd, (struct sockaddr *)&addr, &addr_size) < 0) {
    ESP_LOGE(tag, "Error getting client IP");
  } else {
    // read the IPv4 remote IP
    inet_ntop(AF_INET, &addr.sin6_addr.un.u32_addr[3], ipstr, sizeof(ipstr));
  }
  ESP_LOGI(tag, "GET / from %s", ipstr);
  httpd_resp_set_type(req, "text/html");
  httpd_resp_sendstr(req, "Hello world");
  return ESP_OK;
}

void init_http_server() {
  // initialize http server
  rest_server_context_t *restContext =
      (rest_server_context_t *)calloc(1, sizeof(rest_server_context_t));
  httpd_handle_t server = NULL;
  httpd_config_t serverConfig = HTTPD_DEFAULT_CONFIG();
  serverConfig.uri_match_fn = httpd_uri_match_wildcard;
  ESP_LOGI(tag, "Starting HTTP Server");
  if (esp_err_t err = httpd_start(&server, &serverConfig) != ESP_OK) {
    ESP_LOGE(tag, "Start server failed: %s", err);
  }
  httpd_uri_t root_get_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = root_get_handler,
      .user_ctx = restContext,
  };
  httpd_register_uri_handler(server, &root_get_uri);
}

void wifi_connect_task(void *pvParameters) {

  /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or
   * connection failed for the maximum number of re-tries (WIFI_FAIL_BIT). The
   * bits are set by event_handler() (see above) */
  EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                         WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                         pdFALSE, pdFALSE, portMAX_DELAY);
  if (bits & WIFI_CONNECTED_BIT) {
    ESP_LOGI(tag, "Connected to Wifi SSID: %s", WIFI_SSID);
  } else if (bits & WIFI_FAIL_BIT) {
    ESP_LOGE(tag, "Failed to connect to SSID: %s", WIFI_SSID);
  } else {
    ESP_LOGE(tag, "Unexpected event: %02x", bits);
  }
  vTaskDelete(wifi_task_handle);
  wifi_task_handle = NULL;
}

void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id,
                   void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    ESP_LOGI(tag, "Wifi STA start");
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGE(tag, "Wifi disconnect. Retrying %d/%d", wifi_connect_retry_counter,
             MAX_WIFI_CONNECT_RETRIES);
    if (wifi_connect_retry_counter++ < MAX_WIFI_CONNECT_RETRIES) {
      esp_wifi_connect();
    } else {
      // xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
      ESP_LOGI(tag, "Waiting %ds until retry", WIFI_CONNECT_RETRY_DELAY_SEC);
      wifi_connect_retry_counter = 0;
      vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECT_RETRY_DELAY_SEC * 1000));
      esp_wifi_connect();
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(tag, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
    ESP_LOGI(tag, "Wifi STA connected");
    wifi_connect_retry_counter = 0;
    // TODO: Trigger NTP time retrieval
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_HOME_CHANNEL_CHANGE) {
    ESP_LOGI(tag, "Wifi home channel change");
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_BEACON_TIMEOUT) {
    ESP_LOGI(tag, "Beacon timeout. Reconnecting...");
    vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECT_RETRY_DELAY_SEC * 1000));
    esp_wifi_connect();
  } else {
    ESP_LOGE(tag, "Unknown event: %s id: %d. Trying reconnect", event_base,
             event_id);
    vTaskDelay(pdMS_TO_TICKS(WIFI_CONNECT_RETRY_DELAY_SEC * 1000));
    esp_wifi_connect();
    return;
  }
}

void wifi_init_sta() {
  s_wifi_event_group = xEventGroupCreate();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  esp_event_handler_instance_t any_id;
  esp_event_handler_instance_t got_ip;
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &any_id));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &got_ip));
  wifi_config_t wifi_config;
  memset(&wifi_config, 0, sizeof(wifi_config));
  strcpy((char *)wifi_config.sta.ssid, WIFI_SSID);
  strcpy((char *)wifi_config.sta.password, WIFI_PASSWORD);

  wifi_config.sta.bssid_set = false;
  wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
  wifi_config.sta.pmf_cfg.capable = true;
  wifi_config.sta.pmf_cfg.required = false;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(tag, "wifi_init_sta finished.");
  // TODO: Task which waits for wifi connection not needed
  xTaskCreate(wifi_connect_task, "WIFI_CONNECT", 0, NULL, tskIDLE_PRIORITY,
              &wifi_task_handle);
}
