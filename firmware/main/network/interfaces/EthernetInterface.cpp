/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2024 Mooneer Salem
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

#include "EthernetInterface.h"

#include <cstring>

#include "esp_mac.h"
#include "esp_log.h"
#include "esp_event.h"

#define CURRENT_LOG_TAG "EthernetInterface"
#define ETHERNET_SPI_HOST (SPI2_HOST)
#define ETHERNET_CLOCK_SPEED_HZ (SPI_MASTER_FREQ_40M)
#define GPIO_ETHERNET_SPI_SCLK (42)
#define GPIO_ETHERNET_SPI_MISO (41)
#define GPIO_ETHERNET_SPI_MOSI (40)
#define GPIO_ETHERNET_SPI_CS (39)
#define GPIO_ETHERNET_INTERRUPT (-1) /* polling only */
#define ETHERNET_POLLING_PERIOD_MS (5)

namespace ezdv
{

namespace network
{
    
namespace interfaces
{

EthernetInterface::EthernetInterface()
    : ethDeviceHandle_(nullptr)
{
    // empty
}

EthernetInterface::~EthernetInterface()
{
    // empty
}

void EthernetInterface::bringUp()
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
    w5500_config.poll_period_ms = ETHERNET_POLLING_PERIOD_MS;
    esp_eth_mac_t *mac = esp_eth_mac_new_w5500(&w5500_config, &mac_config);
    esp_eth_phy_t *phy = esp_eth_phy_new_w5500(&phy_config);
    
    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy); // apply default driver configuration
    esp_eth_driver_install(&config, &ethDeviceHandle_); // install driver
    
    if (ethDeviceHandle_ == nullptr)
    {
        ESP_LOGW(CURRENT_LOG_TAG, "could not install driver");
        esp_eth_driver_uninstall(ethDeviceHandle_);
        return;
    }
        
    // Create network interface for Ethernet
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH(); // apply default network interface configuration for Ethernet
    interfaceHandle_ = esp_netif_new(&cfg); // create network interface for Ethernet driver
    if (interfaceHandle_ == nullptr)
    {
        ESP_LOGW(CURRENT_LOG_TAG, "Could not initialize Ethernet interface");
        esp_eth_driver_uninstall(ethDeviceHandle_);
        ethDeviceHandle_ = nullptr;
    }
    else
    {
        ESP_ERROR_CHECK(esp_event_handler_instance_register(ETH_EVENT, 
                                                            ESP_EVENT_ANY_ID, 
                                                            &EthernetEventHandler_, 
                                                            this,
                                                            NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                            ESP_EVENT_ANY_ID,
                                                            &IPEventHandler_,
                                                            this,
                                                            NULL));
        
        // Set MAC address if one isn't already assigned
        uint8_t tmpMac[6];
        uint8_t zeroMac[6];
        memset(zeroMac, 0, sizeof(zeroMac));
        
        getMacAddress(tmpMac);
        if (memcmp(tmpMac, zeroMac, sizeof(zeroMac)) == 0)
        {
            ESP_ERROR_CHECK(esp_eth_ioctl(ethDeviceHandle_, ETH_CMD_S_MAC_ADDR, local_mac_1));
        }
        
        auto glue = esp_eth_new_netif_glue(ethDeviceHandle_);
        if (!glue)
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Could not create glue object");
            esp_netif_destroy(interfaceHandle_);
            esp_eth_driver_uninstall(ethDeviceHandle_);
            
            interfaceHandle_ = nullptr;
            ethDeviceHandle_ = nullptr;
        }
        else
        {        
            esp_netif_attach(interfaceHandle_, glue); // attach Ethernet driver to TCP/IP stack
            esp_eth_start(ethDeviceHandle_); // start Ethernet driver state machine
        }
    }
}

void EthernetInterface::tearDown()
{
    esp_eth_stop(ethDeviceHandle_);
    esp_netif_destroy(interfaceHandle_);
    esp_eth_driver_uninstall(ethDeviceHandle_);
    
    esp_event_handler_instance_unregister(ETH_EVENT,
                                         ESP_EVENT_ANY_ID,
                                         &ethEventHandle_);
    esp_event_handler_instance_unregister(IP_EVENT,
                                          ESP_EVENT_ANY_ID,
                                          &ipEventHandle_);

    interfaceHandle_ = nullptr;
    ethDeviceHandle_ = nullptr;
}

void EthernetInterface::getMacAddress(uint8_t* mac)
{
    ESP_ERROR_CHECK(esp_eth_ioctl(ethDeviceHandle_, ETH_CMD_G_MAC_ADDR, mac));
}

void EthernetInterface::IPEventHandler_(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ESP_LOGI(CURRENT_LOG_TAG, "IP event: %ld", event_id);
    
    EthernetInterface* obj = (EthernetInterface*)event_handler_arg;
    
    switch(event_id)
    {
        case IP_EVENT_ETH_GOT_IP:
        {
            ip_event_got_ip_t* ipData = (ip_event_got_ip_t*)event_data;            
            if (ipData->esp_netif == obj->interfaceHandle_)
            {
                char buf[32];
                sprintf(buf, IPSTR, IP2STR(&ipData->ip_info.ip));
            
                ESP_LOGI(CURRENT_LOG_TAG, "Got IP address %s from DHCP server", buf);
        
                obj->status_ = INTERFACE_UP;
                if (obj->onNetworkUpFn_)
                {
                    obj->onNetworkUpFn_(*obj);
                }
            
                if (obj->onIpAddressAssignedFn_)
                {
                    obj->onIpAddressAssignedFn_(*obj, buf);
                }
            }
            break;
        }
    }
}

void EthernetInterface::EthernetEventHandler_(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    EthernetInterface* obj = (EthernetInterface*)arg;
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
        obj->status_ = INTERFACE_DOWN;
        
        if (obj->onNetworkDownFn_)
        {
            obj->onNetworkDownFn_(*obj);
        }
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(CURRENT_LOG_TAG, "Ethernet Started");        
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(CURRENT_LOG_TAG, "Ethernet Stopped");
        
        obj->status_ = INTERFACE_DOWN;
        
        if (obj->onNetworkDownFn_)
        {
            obj->onNetworkDownFn_(*obj);
        }
        break;
    default:
        break;
    }
}

}

}

}