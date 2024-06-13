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

#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include <dirent.h>

#include "esp_err.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"

#include "esp_http_server.h"
#include "esp_sntp.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_log.h"

#include "hal/eth_types.h"
#include "esp_eth.h"
#include "esp_eth_driver.h"

#include "WirelessTask.h"
#include "HttpServerTask.h"
#include "NetworkMessage.h"

#define DEFAULT_AP_NAME_PREFIX "ezDV "
#define DEFAULT_AP_CHANNEL (1)
#define MAX_AP_CONNECTIONS (5)

/* Max length a file path can have on storage */
#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + CONFIG_SPIFFS_OBJ_NAME_LEN)
#define SCRATCH_BUFSIZE 256
#define CURRENT_LOG_TAG "WirelessTask"

#define ETHERNET_SPI_HOST (SPI2_HOST)
#define ETHERNET_CLOCK_SPEED_HZ (SPI_MASTER_FREQ_16M)
#define GPIO_ETHERNET_SPI_SCLK (42)
#define GPIO_ETHERNET_SPI_MISO (41)
#define GPIO_ETHERNET_SPI_MOSI (40)
#define GPIO_ETHERNET_SPI_CS (39)
#define GPIO_ETHERNET_INTERRUPT (38)

extern "C"
{
    DV_EVENT_DEFINE_BASE(WIRELESS_TASK_MESSAGE);
}

namespace ezdv
{

namespace network
{

void WirelessTask::EthernetEventHandler_(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    /* we can get the ethernet driver handle from event data */
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        ESP_LOGI(CURRENT_LOG_TAG, "Ethernet Link Up");
        ESP_LOGI(CURRENT_LOG_TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x",
                    mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(CURRENT_LOG_TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(CURRENT_LOG_TAG, "Ethernet Started");        
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(CURRENT_LOG_TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}
    
void WirelessTask::IPEventHandler_(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGI(CURRENT_LOG_TAG, "IP event: %ld", event_id);
    
    WirelessTask* obj = (WirelessTask*)event_handler_arg;
    
    if (obj->isAwake())
    {
        switch(event_id)
        {
            case IP_EVENT_AP_STAIPASSIGNED:
            {
                ip_event_ap_staipassigned_t* ipData = (ip_event_ap_staipassigned_t*)event_data;
                char buf[32];
                sprintf(buf, IPSTR, IP2STR(&ipData->ip));
                
                ESP_LOGI(CURRENT_LOG_TAG, "Assigned IP %s to client", buf);
                ApAssignedIpMessage message(buf, ipData->mac);
                obj->post(&message);

                break;
            }
            case IP_EVENT_STA_GOT_IP:
            case IP_EVENT_ETH_GOT_IP:
            {
                ip_event_got_ip_t* ipData = (ip_event_got_ip_t*)event_data;
                char buf[32];
                sprintf(buf, "IP " IPSTR, IP2STR(&ipData->ip_info.ip));
            
                StaAssignedIpMessage message(buf);
                obj->post(&message);

                break;
            }
        }
    }
}

void WirelessTask::WiFiEventHandler_(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    WirelessTask* obj = (WirelessTask*)event_handler_arg;
    
    ESP_LOGI(CURRENT_LOG_TAG, "Wi-Fi event: %ld", event_id);
    
    if (obj->isAwake())
    {
        switch (event_id)
        {
            case WIFI_EVENT_SCAN_DONE:
            {
                WifiScanCompletedMessage message;
                obj->post(&message);
                break;
            }
            case WIFI_EVENT_AP_START:
            {
                ApStartedMessage message;
                obj->post(&message);
                break;
            }
            case WIFI_EVENT_AP_STOP:
            case WIFI_EVENT_STA_DISCONNECTED:
            {
                bool networkIsDown = true;

                if (event_id == WIFI_EVENT_STA_DISCONNECTED)
                {
                    wifi_event_sta_disconnected_t *disconn = (wifi_event_sta_disconnected_t*)event_data;
                    if (disconn->reason == WIFI_REASON_ROAMING) 
                    {
                        ESP_LOGW(CURRENT_LOG_TAG, "Network disconnected due to roaming");
                        networkIsDown = false;
                    }
                }

                if (networkIsDown)
                {
                    NetworkDownMessage message;
                    obj->post(&message);
                }

                break;
            }
            case WIFI_EVENT_AP_STADISCONNECTED:
            {
                wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)event_data;
                DeviceDisconnectedMessage message(event->mac);
                obj->post(&message);
                break;
            }
        }
    }
}

WirelessTask::WirelessTask(audio::AudioInput* freedvHandler, audio::AudioInput* tlv320Handler, audio::AudioInput* audioMixer, audio::VoiceKeyerTask* vkTask)
    : ezdv::task::DVTask("WirelessTask", 5, 4096, tskNO_AFFINITY, 128)
    , wifiScanTimer_(this, this, &WirelessTask::triggerWifiScan_, 5000000, "WifiScanTimer") // 5 seconds between Wi-Fi scans
    , icomRestartTimer_(this, this, &WirelessTask::restartIcomConnection_, 10000000, "IcomRestartTimer") // 10 seconds, then restart Icom control task.
    , icomControlTask_(nullptr)
    , icomAudioTask_(nullptr)
    , icomCIVTask_(nullptr)
    , flexTcpTask_(nullptr)
    , flexVitaTask_(nullptr)
    , freedvHandler_(freedvHandler)
    , tlv320Handler_(tlv320Handler)
    , audioMixerHandler_(audioMixer)
    , vkTask_(vkTask)
    , isAwake_(false)
    , overrideWifiSettings_(false)
    , wifiRunning_(false)
    , radioRunning_(false)
{
    registerMessageHandler(this, &WirelessTask::onRadioStateChange_);
    registerMessageHandler(this, &WirelessTask::onWifiSettingsMessage_);

    registerMessageHandler(this, &WirelessTask::onWifiScanStartMessage_);
    registerMessageHandler(this, &WirelessTask::onWifiScanStopMessage_);

    // Handlers for internal messages (intended to make events that happen
    // on ESP-IDF tasks happen on this one instead).
    registerMessageHandler(this, &WirelessTask::onApAssignedIpMessage_);
    registerMessageHandler(this, &WirelessTask::onStaAssignedIpMessage_);
    registerMessageHandler(this, &WirelessTask::onWifiScanCompletedMessage_);
    registerMessageHandler(this, &WirelessTask::onApStartedMessage_);
    registerMessageHandler(this, &WirelessTask::onNetworkDownMessage_);
    registerMessageHandler(this, &WirelessTask::onDeviceDisconnectedMessage_);
}

WirelessTask::~WirelessTask()
{
    // empty
}

void WirelessTask::setWiFiOverride(bool wifiOverride)
{
    overrideWifiSettings_ = wifiOverride;
}

void WirelessTask::onTaskStart_()
{
    isAwake_ = true;
}

void WirelessTask::onTaskSleep_()
{
    isAwake_ = false;
    
    // Stop reporting
    if (freeDVReporterTask_.isAwake())
    {
        sleep(&freeDVReporterTask_, pdMS_TO_TICKS(2000));
    }

    if (pskReporterTask_.isAwake())
    {
        sleep(&pskReporterTask_, pdMS_TO_TICKS(1000));
    }

    disableHttp_();
        
    // Audio and CIV need to stop before control
    if (icomAudioTask_ != nullptr)
    {
        sleep(icomAudioTask_, pdMS_TO_TICKS(1000));
        delete icomAudioTask_;
        icomAudioTask_ = nullptr;
    }

    if (icomCIVTask_ != nullptr)
    {
        sleep(icomCIVTask_, pdMS_TO_TICKS(1000));
        delete icomCIVTask_;
        icomCIVTask_ = nullptr;
    }

    if (icomControlTask_ != nullptr)
    {
        sleep(icomControlTask_, pdMS_TO_TICKS(1000));
        delete icomControlTask_;
        icomControlTask_ = nullptr;
    }

    if (flexVitaTask_ != nullptr)
    {
        sleep(flexVitaTask_, pdMS_TO_TICKS(1000));
        delete flexVitaTask_;
        flexVitaTask_ = nullptr;
    }

    if (flexTcpTask_ != nullptr)
    {
        sleep(flexTcpTask_, pdMS_TO_TICKS(1000));
        delete flexTcpTask_;
        flexTcpTask_ = nullptr;
    }
    
    disableWifi_();
}

void WirelessTask::enableDefaultWifi_()
{
    esp_netif_create_default_wifi_ap();
    
    // Register event handler so we can notify the user on network
    // status changes.
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WiFiEventHandler_,
                                                        this,
                                                        &wifiEventHandle_));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &IPEventHandler_,
                                                        this,
                                                        &ipEventHandle_));
                                                        
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));

    wifi_config.ap.ssid_len = 0; // Will auto-determine length on start.
    wifi_config.ap.channel = DEFAULT_AP_CHANNEL;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    wifi_config.ap.max_connection = MAX_AP_CONNECTIONS;
    wifi_config.ap.pmf_cfg.required = false;
    
    // Append last two bytes of MAC address to default SSID.
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_AP, mac));
    sprintf((char*)wifi_config.ap.ssid, "%s%02x%02x", DEFAULT_AP_NAME_PREFIX, mac[4], mac[5]);
    
    sprintf((char*)wifi_config.ap.password, "%s", "");
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void WirelessTask::enableWifi_(storage::WifiMode mode, storage::WifiSecurityMode security, int channel, char* ssid, char* password, char* hostname)
{    
    esp_netif_t* netif = nullptr;
    if (mode == storage::WifiMode::ACCESS_POINT)
    {
        netif = esp_netif_create_default_wifi_ap();
    }
    else if (mode == storage::WifiMode::CLIENT)
    {
        netif = esp_netif_create_default_wifi_sta();
    }
    else
    {
        assert(0);
    }

    // Set hostname as configured by the user
    if (hostname != nullptr && strlen(hostname) > 0)
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Setting ezDV hostname to %s", hostname);
        ESP_ERROR_CHECK(esp_netif_set_hostname(netif, hostname));
    }

    ESP_LOGI(CURRENT_LOG_TAG, "Setting ezDV SSID to %s", ssid);

    // Register event handler so we can notify the user on network
    // status changes.
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WiFiEventHandler_,
                                                        this,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &IPEventHandler_,
                                                        this,
                                                        NULL));
                                                        
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Disable power save.
    //esp_wifi_set_ps(WIFI_PS_NONE);

    if (mode == storage::WifiMode::ACCESS_POINT)
    {
        wifi_auth_mode_t auth_mode = WIFI_AUTH_OPEN;
        
        switch(security)
        {
            case storage::WifiSecurityMode::NONE:
                auth_mode = WIFI_AUTH_OPEN;
                break;
            case storage::WifiSecurityMode::WEP:
                auth_mode = WIFI_AUTH_WEP;
                break;
            case storage::WifiSecurityMode::WPA:
                auth_mode = WIFI_AUTH_WPA_PSK;
                break;
            case storage::WifiSecurityMode::WPA2:
                auth_mode = WIFI_AUTH_WPA2_PSK;
                break;
            case storage::WifiSecurityMode::WPA_AND_WPA2:
                auth_mode = WIFI_AUTH_WPA_WPA2_PSK;
                break;
#if 0
            case storage::WifiSecurityMode::WPA3:
                auth_mode = WIFI_AUTH_WPA3_PSK;
                break;
            case storage::WifiSecurityMode::WPA2_AND_WPA3:
                auth_mode = WIFI_AUTH_WPA2_WPA3_PSK;
                break;
#endif // 0
            default:
                assert(0);
                break;
        }
        
        wifi_config_t wifi_config;
        memset(&wifi_config, 0, sizeof(wifi_config_t));

        wifi_config.ap.ssid_len = 0; // Will auto-determine length on start.
        wifi_config.ap.channel = (uint8_t)channel;
        wifi_config.ap.authmode = auth_mode;
        wifi_config.ap.max_connection = MAX_AP_CONNECTIONS;
        wifi_config.ap.pmf_cfg.required = false;
        
        sprintf((char*)wifi_config.ap.ssid, "%s", ssid);
        sprintf((char*)wifi_config.ap.password, "%s", password);
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

        // Force to HT20 mode to better handle congested airspace
        ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20));

        ESP_ERROR_CHECK(esp_wifi_start());
    }
    else
    {
        wifi_config_t wifi_config;
        memset(&wifi_config, 0, sizeof(wifi_config_t));

        wifi_config.sta.scan_method = WIFI_FAST_SCAN;
        wifi_config.sta.bssid_set = false;
        memset(wifi_config.sta.bssid, 0, sizeof(wifi_config.sta.bssid));
        wifi_config.sta.channel = 0;
        wifi_config.sta.listen_interval = 0;
        wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

        // Enable fast roaming (typically for mesh networks or enterprise setups)
        wifi_config.sta.btm_enabled = 1;
        wifi_config.sta.rm_enabled = 1;
        wifi_config.sta.mbo_enabled = 1;
        wifi_config.sta.ft_enabled = 1;
        
        sprintf((char*)wifi_config.sta.ssid, "%s", ssid);
        sprintf((char*)wifi_config.sta.password, "%s", password);
        
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

        // Force to HT20 mode to better handle congested airspace
        ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20));

        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_ERROR_CHECK(esp_wifi_connect());
    }
}

void WirelessTask::enableEthernet_()
{
    // Generate MAC address
    uint8_t base_mac_addr[ETH_ADDR_LEN];
    ESP_ERROR_CHECK(esp_efuse_mac_get_default(base_mac_addr));
    uint8_t local_mac_1[ETH_ADDR_LEN];
    esp_derive_local_mac(local_mac_1, base_mac_addr);
    
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();      // apply default common MAC configuration
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();      // apply default PHY configuration
    phy_config.phy_addr = 1;                                     // alter the PHY address according to your board design
    phy_config.reset_gpio_num = -1;                              // alter the GPIO used for PHY reset

    // SPI bus configuration
    spi_bus_config_t buscfg;
    buscfg.mosi_io_num = GPIO_ETHERNET_SPI_MOSI;
    buscfg.miso_io_num = GPIO_ETHERNET_SPI_MISO;
    buscfg.sclk_io_num = GPIO_ETHERNET_SPI_SCLK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.data4_io_num = -1;
    buscfg.data5_io_num = -1;
    buscfg.data6_io_num = -1;
    buscfg.data7_io_num = -1;
    buscfg.max_transfer_sz = 0; // use default
    buscfg.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS;
    buscfg.isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO;
    buscfg.intr_flags = 0;
    ESP_ERROR_CHECK(spi_bus_initialize(ETHERNET_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    
    // Configure SPI device
    spi_device_interface_config_t spi_devcfg;
    memset(&spi_devcfg, 0, sizeof(spi_device_interface_config_t));
    spi_devcfg.mode = 0;
    spi_devcfg.clock_speed_hz = ETHERNET_CLOCK_SPEED_HZ;
    spi_devcfg.spics_io_num = GPIO_ETHERNET_SPI_CS;
    spi_devcfg.queue_size = 20;
    spi_devcfg.command_bits = 0;
    spi_devcfg.address_bits = 0;
    spi_devcfg.clock_source = SPI_CLK_SRC_DEFAULT;
    
    /* W5500 ethernet driver is based on spi driver */
    eth_w5500_config_t w5500_config = ETH_W5500_DEFAULT_CONFIG(ETHERNET_SPI_HOST, &spi_devcfg);
    w5500_config.int_gpio_num = GPIO_ETHERNET_INTERRUPT;
    //w5500_config.poll_period_ms = 5;
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
    
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy); // apply default driver configuration
    esp_eth_handle_t eth_handle = nullptr; // after the driver is installed, we will get the handle of the driver
    esp_eth_driver_install(&config, &eth_handle); // install driver
    
    if (eth_handle == nullptr)
    {
        ESP_LOGW(CURRENT_LOG_TAG, "could not install driver");
        return;
    }
        
    // Create network interface for Ethernet
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH(); // apply default network interface configuration for Ethernet
    esp_netif_t *eth_netif = esp_netif_new(&cfg); // create network interface for Ethernet driver
    if (eth_netif == nullptr)
    {
        ESP_LOGW(CURRENT_LOG_TAG, "Could not initialize Ethernet interface");
    }
    else
    {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(ETH_EVENT, 
                                                            ESP_EVENT_ANY_ID, 
                                                            &EthernetEventHandler_, 
                                                            this,
                                                            NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &WiFiEventHandler_,
                                                            this,
                                                            NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &IPEventHandler_,
                                                            this,
                                                            NULL));
        
        // Set MAC address
        ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, local_mac_1));
        
        auto glue = esp_eth_new_netif_glue(eth_handle);
        if (!glue)
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Could not create glue object");
        }
        else
        {        
            esp_netif_attach(eth_netif, glue); // attach Ethernet driver to TCP/IP stack
            esp_eth_start(eth_handle); // start Ethernet driver state machine
        }
    }
}

void WirelessTask::disableWifi_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "Shutting down Wi-Fi");

    // Shut down SNTP.
    if (esp_sntp_enabled())
    {
        esp_sntp_stop();
    }
    
    esp_event_handler_instance_unregister(WIFI_EVENT,
                                            ESP_EVENT_ANY_ID,
                                            &wifiEventHandle_);
    esp_event_handler_instance_unregister(IP_EVENT,
                                            ESP_EVENT_ANY_ID,
                                            &ipEventHandle_);

    esp_wifi_disconnect();
    esp_wifi_stop();
    
    // Disable Ethernet as well - TBD
}

void WirelessTask::enableHttp_()
{
    httpServerTask_.start();
}

void WirelessTask::disableHttp_()
{
    if (wifiRunning_)
    {
        sleep(&httpServerTask_, pdMS_TO_TICKS(1000));
    }
}

void WirelessTask::onNetworkUp_()
{
    WirelessNetworkStatusMessage message(true);
    publish(&message);

    wifiRunning_ = true;
    
    enableHttp_();
}

void WirelessTask::onNetworkConnected_(bool client, char* ip, uint8_t* macAddress)
{
    if (radioRunning_)
    {
        // Don't reexecute if the radio is already running.
        return;
    }
    
    // Broadcast our current IP address if available.
    if (client)
    {
        IpAddressAssignedMessage ipAssigned(ip);
        publish(&ipAssigned);
    }
    else
    {
        IpAddressAssignedMessage ipAssigned("");
        publish(&ipAssigned);
    }
    
    // Start the VITA task here since we need it to be able to 
    // get UDP broadcasts from the radio.
    if (flexVitaTask_ == nullptr)
    {
        flexVitaTask_ = new flex::FlexVitaTask();
        start(flexVitaTask_, pdMS_TO_TICKS(1000));
    }
    
    // Get the current Icom radio settings
    if (!overrideWifiSettings_)
    {
        if (!freeDVReporterTask_.isAwake())
        {
            start(&freeDVReporterTask_, pdMS_TO_TICKS(1000));
        }

        if (!pskReporterTask_.isAwake())
        {
            start(&pskReporterTask_, pdMS_TO_TICKS(1000));
        }
        
        storage::RequestRadioSettingsMessage request;
        publish(&request);

        auto response = waitFor<storage::RadioSettingsMessage>(pdMS_TO_TICKS(2000), nullptr);
        if (response != nullptr)
        {
            if (response->enabled && !radioRunning_)
            {
                if (client || !strcmp(response->host, ip))
                {
                    // Grab MAC address for later use when disconnecting
                    // (to prevent reconnection when the radio is obviously offline).
                    if (macAddress != nullptr)
                    {
                        memcpy(radioMac_, macAddress, sizeof(radioMac_));
                    }
                    
                    radioType_ = response->type;
                    switch (response->type)
                    {
                        case 0:
                        {
                            ESP_LOGI(CURRENT_LOG_TAG, "Starting Icom radio connectivity");

                            icomControlTask_ = new icom::IcomSocketTask(icom::IcomSocketTask::CONTROL_SOCKET);
                            icomAudioTask_ = new icom::IcomSocketTask(icom::IcomSocketTask::AUDIO_SOCKET);
                            icomCIVTask_ = new icom::IcomSocketTask(icom::IcomSocketTask::CIV_SOCKET);

                            // Wait a bit, then start the connection.
                            icomRestartTimer_.start(true);
                            radioRunning_ = true;
                            break;
                        }
                        case 1:
                        {
                            ESP_LOGI(CURRENT_LOG_TAG, "Starting FlexRadio connectivity");

                            flexTcpTask_ = new flex::FlexTcpTask();
                            start(flexTcpTask_, pdMS_TO_TICKS(1000));

                            flex::FlexConnectRadioMessage connectMessage(response->host);
                            publish(&connectMessage);

                            radioRunning_ = true;
                            break;
                        }
                        default:
                        {
                            ESP_LOGW(CURRENT_LOG_TAG, "Unknown radio type %d, ignoring", response->type);
                            break;
                        }
                    }
                }
            }
            else
            {
                ESP_LOGI(CURRENT_LOG_TAG, "Radio connectivity disabled");
            }
            
            delete response;
        }
        else
        {
            ESP_LOGW(CURRENT_LOG_TAG, "Timed out waiting for radio connection settings!");
        }
    }
}

void WirelessTask::onNetworkDisconnected_()
{
    // Stop Icom reset timer if needed.
    icomRestartTimer_.stop();
    
    // Force immediate state transition to idle for the radio tasks.
    if (freeDVReporterTask_.isAwake())
    {
        sleep(&freeDVReporterTask_, pdMS_TO_TICKS(2000));
    }
    
    if (pskReporterTask_.isAwake())
    {
        sleep(&pskReporterTask_, pdMS_TO_TICKS(1000));
    }

    if (icomControlTask_ != nullptr)
    {
        sleep(icomControlTask_, pdMS_TO_TICKS(1000));
        delete icomControlTask_;
        icomControlTask_ = nullptr;
    }

    if (icomAudioTask_ != nullptr)
    {
        sleep(icomAudioTask_, pdMS_TO_TICKS(1000));
        delete icomAudioTask_;
        icomAudioTask_ = nullptr;
    }

    if (icomCIVTask_ != nullptr)
    {
        sleep(icomCIVTask_, pdMS_TO_TICKS(1000));
        delete icomCIVTask_;
        icomCIVTask_ = nullptr;
    }

    if (flexTcpTask_ != nullptr)
    {
        sleep(flexTcpTask_, pdMS_TO_TICKS(5000));
        delete flexTcpTask_;
        flexTcpTask_ = nullptr;
    }

    if (flexVitaTask_ != nullptr)
    {
        sleep(flexVitaTask_, pdMS_TO_TICKS(1000));
        delete flexVitaTask_;
        flexVitaTask_ = nullptr;
    }

    // Shut down HTTP server.
    disableHttp_();

    WirelessNetworkStatusMessage message(false);
    publish(&message);
}

void WirelessTask::onRadioStateChange_(DVTask* origin, RadioConnectionStatusMessage* message)
{
    if (message->state)
    {
        ESP_LOGI(CURRENT_LOG_TAG, "rerouting audio pipes to network");
        
        tlv320Handler_->setAudioOutput(
            audio::AudioInput::ChannelLabel::RIGHT_CHANNEL,
            nullptr
        );
        
        if (radioType_ == 0)
        {
            icomAudioTask_->setAudioOutput(
                audio::AudioInput::ChannelLabel::LEFT_CHANNEL, 
                freedvHandler_->getAudioInput(audio::AudioInput::ChannelLabel::RADIO_CHANNEL)
            );
        
            freedvHandler_->setAudioOutput(
                audio::AudioInput::ChannelLabel::RADIO_CHANNEL, 
                icomAudioTask_->getAudioInput(audio::AudioInput::ChannelLabel::LEFT_CHANNEL)
            );
        }
        else if (radioType_ == 1)
        {
            // Flex 100% goes through SmartSDR, so disable TLV320 user port handling
            tlv320Handler_->setAudioOutput(
                audio::AudioInput::ChannelLabel::LEFT_CHANNEL,
                nullptr
            );
            
            // Make sure voice keyer can restore Flex mic device once done.
            vkTask_->setMicDeviceTask(flexVitaTask_);
            
            flexVitaTask_->setAudioOutput(
                audio::AudioInput::ChannelLabel::LEFT_CHANNEL,
                freedvHandler_->getAudioInput(audio::AudioInput::ChannelLabel::USER_CHANNEL)
            );
                
            flexVitaTask_->setAudioOutput(
                audio::AudioInput::ChannelLabel::RIGHT_CHANNEL,
                freedvHandler_->getAudioInput(audio::AudioInput::ChannelLabel::RADIO_CHANNEL)
            );
                
            freedvHandler_->setAudioOutput(
                audio::AudioInput::ChannelLabel::RADIO_CHANNEL, 
                flexVitaTask_->getAudioInput(audio::AudioInput::ChannelLabel::RADIO_CHANNEL)
            );
                
            audioMixerHandler_->setAudioOutput(
                audio::AudioInput::ChannelLabel::LEFT_CHANNEL,
                flexVitaTask_->getAudioInput(audio::AudioInput::ChannelLabel::USER_CHANNEL)
            );
        }
    }
    else
    {
        ESP_LOGI(CURRENT_LOG_TAG, "rerouting audio pipes internally");

        // Make sure voice keyer can restore TLV320 mic device once done.
        vkTask_->setMicDeviceTask(tlv320Handler_);
        
        tlv320Handler_->setAudioOutput(
            audio::AudioInput::ChannelLabel::LEFT_CHANNEL, 
            freedvHandler_->getAudioInput(audio::AudioInput::ChannelLabel::LEFT_CHANNEL)
        );
            
        tlv320Handler_->setAudioOutput(
            audio::AudioInput::ChannelLabel::RIGHT_CHANNEL, 
            freedvHandler_->getAudioInput(audio::AudioInput::ChannelLabel::RIGHT_CHANNEL)
        );
        
        if (icomAudioTask_ != nullptr)
        {
            icomAudioTask_->setAudioOutput(
                audio::AudioInput::ChannelLabel::LEFT_CHANNEL, 
                nullptr
            );
        }

        freedvHandler_->setAudioOutput(
            audio::AudioInput::ChannelLabel::RADIO_CHANNEL, 
            tlv320Handler_->getAudioInput(audio::AudioInput::ChannelLabel::RADIO_CHANNEL)
        );
            
        audioMixerHandler_->setAudioOutput(
            audio::AudioInput::ChannelLabel::LEFT_CHANNEL,
            tlv320Handler_->getAudioInput(audio::AudioInput::ChannelLabel::USER_CHANNEL)
        );
            
        if (radioType_ == 0)
        {
            // If Icom, we'll need to wake up the process again.
            // XXX - this is a special case as it puts itself to sleep as part of the
            // disconnect logic.
            icomRestartTimer_.start(true);
        }
    }
}

void WirelessTask::onWifiSettingsMessage_(DVTask* origin, storage::WifiSettingsMessage* message)
{
    // Avoid accidentally trying to re-initialize Wi-Fi.
    if (!wifiRunning_)
    {
        // Start event loop.
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        
        // Ethernet should always be started in case we're plugged into the network.
        enableEthernet_();
        
        if (overrideWifiSettings_)
        {
            // Setup is *just* different enough that we have to have a separate function for it
            // (we can't get the MAC address w/o bringing up Wi-Fi first, and that's not possible
            // with enableWifi_()).
            enableDefaultWifi_();
        }
        else if (message->enabled)
        {
            enableWifi_(message->mode, message->security, message->channel, message->ssid, message->password, message->hostname);
        }
        else
        {
            // Start Wi-Fi without any connection. This ensures that scanning still works.
            esp_netif_t* netif = esp_netif_create_default_wifi_sta();
            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        
            wifi_config_t wifi_config;
            memset(&wifi_config, 0, sizeof(wifi_config_t));

            wifi_config.sta.scan_method = WIFI_FAST_SCAN;
            wifi_config.sta.bssid_set = false;
            memset(wifi_config.sta.bssid, 0, sizeof(wifi_config.sta.bssid));
            wifi_config.sta.channel = 0;
            wifi_config.sta.listen_interval = 0;
            wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

            // Enable fast roaming (typically for mesh networks or enterprise setups)
            wifi_config.sta.btm_enabled = 1;
            wifi_config.sta.rm_enabled = 1;
            wifi_config.sta.mbo_enabled = 1;
            wifi_config.sta.ft_enabled = 1;
        
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
            ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
            ESP_ERROR_CHECK(esp_wifi_start());
        }
        
        // Start SNTP
        esp_sntp_init();
        esp_sntp_setservername(0, "pool.ntp.org");
    }
}

void WirelessTask::restartIcomConnection_(DVTimer*)
{
    storage::RequestRadioSettingsMessage request;
    publish(&request);
    
    auto response = waitFor<storage::RadioSettingsMessage>(pdMS_TO_TICKS(2000), nullptr);
    if (response != nullptr)
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Starting Icom radio connectivity");

        start(icomControlTask_, pdMS_TO_TICKS(1000));
        start(icomAudioTask_, pdMS_TO_TICKS(1000));
        start(icomCIVTask_, pdMS_TO_TICKS(1000));

        icom::IcomConnectRadioMessage connectMessage(response->host, response->port, response->username, response->password);
        publish(&connectMessage);
    }
    else
    {
        ESP_LOGE(CURRENT_LOG_TAG, "Could not restart Icom processes!");
    }
}

void WirelessTask::triggerWifiScan_(DVTimer*)
{
    // Start Wi-Fi scan using default config. A WIFI_EVENT_SCAN_DONE event
    // will be fired by ESP-IDF once the scan is done.
    ESP_ERROR_CHECK(esp_wifi_scan_start(nullptr, false));
}

void WirelessTask::onWifiScanComplete_()
{
    // Get the number of Wi-Fi networks found. We'll need to use
    // this to allocate the correct amount of RAM to store the Wi-Fi APs found.
    uint16_t numNetworks = 0;
    auto result = esp_wifi_scan_get_ap_num(&numNetworks);
    if (result != ESP_OK && result != ESP_ERR_WIFI_NOT_STARTED)
    {
        // Force a crash.
        ESP_ERROR_CHECK(result);
    }
    else if (result == ESP_ERR_WIFI_NOT_STARTED)
    {
        // If Wi-Fi is not running, there's no point in continuing.
        return;
    }

    // Always ensure we can allocate at least one AP record. This is to 
    // ensure the Wi-Fi code always frees up whatever it allocates to 
    // perform the scan, even if it doesn't find any other networks.
    numNetworks++;

    // Allocate the necessary memory to grab the AP list.
    wifi_ap_record_t* apRecords = (wifi_ap_record_t*)calloc(numNetworks, sizeof(wifi_ap_record_t));
    assert(apRecords != nullptr);

    // Grab the list of access points found.
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&numNetworks, apRecords));

    // Publish list of Wi-Fi networks to interested parties.
    WifiNetworkListMessage message(numNetworks, apRecords);
    publish(&message);

    // Restart Wi-Fi scan after a predefined time interval.
    wifiScanTimer_.start(true);
}

void WirelessTask::onWifiScanStartMessage_(DVTask* origin, StartWifiScanMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Starting Wi-Fi scan");
    
    wifiScanTimer_.stop();
    triggerWifiScan_(nullptr);
}

void WirelessTask::onWifiScanStopMessage_(DVTask* origin, StopWifiScanMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Stopping Wi-Fi scan");
    
    wifiScanTimer_.stop();
}

void WirelessTask::onApAssignedIpMessage_(DVTask* origin, ApAssignedIpMessage* message)
{
    onNetworkConnected_(false, message->ipString, message->macAddress);
}

void WirelessTask::onStaAssignedIpMessage_(DVTask* origin, StaAssignedIpMessage* message)
{
    onNetworkUp_();
    onNetworkConnected_(true, message->ipString, nullptr);
}

void WirelessTask::onWifiScanCompletedMessage_(DVTask* origin, WifiScanCompletedMessage* message)
{
    onWifiScanComplete_();
}

void WirelessTask::onApStartedMessage_(DVTask* origin, ApStartedMessage* message)
{
    onNetworkUp_();
}

void WirelessTask::onNetworkDownMessage_(DVTask* origin, NetworkDownMessage* message)
{
    onNetworkDisconnected_();

    if (isAwake_)
    {
        // Reattempt connection to access point if we couldn't find
        // it the first time around.
        wifiRunning_ = false;
        radioRunning_ = false;
        esp_wifi_disconnect();
        ESP_ERROR_CHECK(esp_wifi_connect());
    }
}

void WirelessTask::onDeviceDisconnectedMessage_(DVTask* origin, DeviceDisconnectedMessage* message)
{
    // Prevent attempted reconnection of radio if that's the device
    // that disconnected.
    if (memcmp(message->macAddress, radioMac_, sizeof(radioMac_)) == 0)
    {
        icomRestartTimer_.stop();
        radioRunning_ = false;
    }
}

}

}
