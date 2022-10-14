/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "driver/gpio.h"

#include "esp_http_client.h"

const int WAIT_LOOP_MS = 100;
const int DEBOUNCE_MS = 100;

bool connected = false;

static const char *TAG = "gcn";

static void gcn_wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        connected = false;
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        connected = false;
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "got ip:%s", ip4addr_ntoa(&event->ip_info.ip));
        connected = true;
    }
}

void wifi_init_sta(void)
{
    tcpip_adapter_init();

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &gcn_wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &gcn_wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_GCN_WIFI_SSID,
            .password = CONFIG_GCN_WIFI_PASSWORD},
    };

    /* Setting a password implies station will connect to all security modes including WEP/WPA.
     * However these modes are deprecated and not advisable to be used. Incase your Access point
     * doesn't support WPA2, these mode can be enabled by commenting below line */

    if (strlen((char *)wifi_config.sta.password))
    {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "wifi_init_sta finished.");
}

esp_err_t gcn_http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer; // Buffer to store response of http request from event handler
    static int output_len;      // Stores number of bytes read

    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;

    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;

    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;

    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;

    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        /*
         *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
         *  However, event handler can also be used in case chunked encoding is used.
         */
        if (!esp_http_client_is_chunked_response(evt->client))
        {
            // If user_data buffer is configured, copy the response into the buffer
            if (output_buffer == NULL)
            {
                int len = esp_http_client_get_content_length(evt->client);
                ESP_LOGI(TAG, "len %i", len);
                output_buffer = (char *)malloc(len + 1);
                output_len = 0;
                if (output_buffer == NULL)
                {
                    ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                    return ESP_FAIL;
                }
            }
            memcpy(output_buffer + output_len, evt->data, evt->data_len);
            output_len += evt->data_len;
        }

        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        if (output_buffer != NULL)
        {
            // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
            // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
            ESP_LOGD(TAG, "output_len %i", output_len);
            if (output_len > 0)
            {
                output_buffer[output_len] = '\0';
                ESP_LOGD(TAG, "output_buffer : %s", output_buffer);
                time_t *srv_time = evt->user_data;
                if (srv_time != NULL)
                    *srv_time = atol(output_buffer);
            }

            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;

    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        if (output_buffer != NULL)
        {
            free(output_buffer);
            output_buffer = NULL;
        }
        output_len = 0;
        break;
    }
    return ESP_OK;
}

void notify_server(int gpio_value)
{
    char buf[512];

    time_t now = time(NULL);
    time_t srv_time = now;

    // prepare client request
    esp_http_client_config_t config = {
        .url = CONFIG_GCN_NOTIFY_URL,
        .method = HTTP_METHOD_POST,
        .transport_type = HTTP_TRANSPORT_OVER_TCP,
        .event_handler = gcn_http_event_handler,
        .user_data = &srv_time,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        ESP_LOGW(TAG, "Could not create HTTP client, skipping notification");
        goto cleanup;
    }

    // build post data
    sprintf(buf, "host=%s&time=%lu&gpio=%i&value=%i", CONFIG_GCN_HOST_NAME, now, CONFIG_GCN_WATCH_GPIO_NUMBER, gpio_value);
    esp_http_client_set_post_field(client, buf, strlen(buf));
    ESP_LOGD(TAG, "query parameters : %s", buf);

    // request
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "HTTP POST request failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    ESP_LOGD(TAG, "Notification result : Status = %d", esp_http_client_get_status_code(client));

    // sync time
    if (abs(srv_time - now) > 60)
    {
        ESP_LOGI(TAG, "Synchronizing local clock (%li) to server clock (%li)", now, srv_time);
        struct timeval tv = {
            .tv_sec = srv_time,
            .tv_usec = 0,
        };
        if (settimeofday(&tv, NULL) != 0)
            ESP_LOGW(TAG, "Could not update local clock");
    }

cleanup:
    if (client != NULL)
        esp_http_client_cleanup(client);
}

void app_main()
{
    if (!GPIO_IS_VALID_GPIO(CONFIG_GCN_WATCH_GPIO_NUMBER))
    {
        ESP_LOGE(TAG, "Invalid GPIO number %i", CONFIG_GCN_WATCH_GPIO_NUMBER);
        abort();
    }

    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << CONFIG_GCN_WATCH_GPIO_NUMBER);
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    bool last_state = gpio_get_level(CONFIG_GCN_WATCH_GPIO_NUMBER);
    time_t last_notification = time(NULL);

    while (true)
    {
        // idle
        vTaskDelay(WAIT_LOOP_MS / portTICK_RATE_MS);

        // read state
        time_t now = time(NULL);
        bool new_state = gpio_get_level(CONFIG_GCN_WATCH_GPIO_NUMBER);

        // heartbeat
        if (connected && now - last_notification > CONFIG_GCN_IDLE_NOTIFICATION_INTERVAL)
        {
            ESP_LOGI(TAG, "Forcing notification as heartbeat");
            notify_server(new_state);
            last_notification = now;
            continue;
        }

        // detect change
        if (new_state == last_state)
            continue;

        // soft-debounce
        vTaskDelay(DEBOUNCE_MS / portTICK_RATE_MS);

        // check that change was not a transient
        new_state = gpio_get_level(CONFIG_GCN_WATCH_GPIO_NUMBER);
        if (new_state == last_state)
            continue;

        // input changed for good, so notify
        last_state = new_state;
        ESP_LOGI(TAG, "GPIO %i changed to %i", CONFIG_GCN_WATCH_GPIO_NUMBER, last_state);
        notify_server(new_state);
    }
}
