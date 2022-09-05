/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2022 Mooneer Salem
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstring>

#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

#include "WirelessTask.h"

#define DEFAULT_AP_NAME_PREFIX "ezDV "
#define DEFAULT_AP_CHANNEL (1)
#define MAX_AP_CONNECTIONS (5)

namespace ezdv
{

namespace network
{
    
WirelessTask::WirelessTask()
    : ezdv::task::DVTask("WirelessTask", 1, 4096, tskNO_AFFINITY, 10)
{
    // empty
}

WirelessTask::~WirelessTask()
{
    // empty
}

void WirelessTask::onTaskStart_()
{
    enableWifi_();
    enableHttp_();
}

void WirelessTask::onTaskWake_()
{
    enableWifi_();
    enableHttp_();
}

void WirelessTask::onTaskSleep_()
{
    disableHttp_();
    disableWifi_();
}

void WirelessTask::enableWifi_()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .password = "",
            .ssid_len = 0, // Will auto-determine length on start.
            .channel = DEFAULT_AP_CHANNEL,
            .authmode = WIFI_AUTH_OPEN,
            .max_connection = MAX_AP_CONNECTIONS,
            .pmf_cfg = {
                    .required = false,
            },
        },
    };
    
    // Append last two bytes of MAC address to SSID prefix.
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, mac));
    sprintf((char*)wifi_config.ap.ssid, "%s%02x%02x", DEFAULT_AP_NAME_PREFIX, mac[4], mac[5]);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void WirelessTask::disableWifi_()
{
    ESP_ERROR_CHECK(esp_wifi_stop());
}

static esp_err_t HelloWorld(httpd_req_t *req)
{
    httpd_resp_send(req, "hello world", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static httpd_uri_t rootPage = 
{
    .uri = "/",
    .method = HTTP_GET,
    .handler = &HelloWorld,
    .user_ctx = nullptr
};

void WirelessTask::enableHttp_()
{
    // Generate default configuration
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    
    // Start HTTP server.
    ESP_ERROR_CHECK(httpd_start(&configServerHandle_, &config));
    
    // Configure URL handlers.
    httpd_register_uri_handler(configServerHandle_, &rootPage);
}

void WirelessTask::disableHttp_()
{
    ESP_ERROR_CHECK(httpd_stop(configServerHandle_));
}

}

}