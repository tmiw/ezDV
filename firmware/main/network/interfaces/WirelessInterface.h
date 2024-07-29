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

#ifndef WIRELESS_INTERFACE_H
#define WIRELESS_INTERFACE_H

#include "INetworkInterface.h"

#include <string>
#include <functional>

#include "esp_netif.h"

// For WifiMode and WifiSecurityMode enums
#include "storage/SettingsMessage.h"

namespace ezdv
{

namespace network
{
    
namespace interfaces
{
    
class WirelessInterface : public INetworkInterface
{
public:
    WirelessInterface();
    virtual ~WirelessInterface();
    
    // Configure Wi-Fi interface
    void configure(storage::WifiMode mode, storage::WifiSecurityMode security, int channel, char* ssid, char* password);
    
    // Shared actions among all interface types
    virtual void bringUp() override;
    virtual void tearDown() override;
    virtual void getMacAddress(uint8_t* mac) override;
    
    // Wi-Fi specific handler for when the AP assigns addresses via DHCP. Assigned IP address is provided.
    void setOnApAssignedIp(OnNetworkIpAssignedType fn);
    
    // Wi-Fi specific handler for when a device disconnects from the AP. MAC address of device is provided.
    using OnWirelessApDeviceDisconnectedType = std::function<void(INetworkInterface&, uint8_t*)>;
    void setOnWirelessApDeviceDisconnected(OnWirelessApDeviceDisconnectedType fn);
    
    // Wi-Fi specific handling for network scanning. Note that the user currently needs to call into the ESP-IDF
    // API to retrieve the networks that were found.
    using OnWirelessScanCompleteType = std::function<void(INetworkInterface&)>;
    void setOnWirelessScanComplete(OnWirelessScanCompleteType fn);
    
    void beginScan();

private:
    OnNetworkIpAssignedType onApAssignedIp_;
    OnWirelessApDeviceDisconnectedType onWirelessApDeviceDisconnected_;
    OnWirelessScanCompleteType onWirelessScanComplete_;
    
    esp_event_handler_instance_t wifiEventHandle_;
    esp_event_handler_instance_t ipEventHandle_;
    bool hasStaConfig_;
    
    static void IPEventHandler_(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
    static void WiFiEventHandler_(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
};

}

}

}

#endif // WIRELESS_INTERFACE_H