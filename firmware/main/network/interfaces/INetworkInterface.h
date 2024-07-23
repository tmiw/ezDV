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

#ifndef I_NETWORK_INTERFACE_H
#define I_NETWORK_INTERFACE_H

#include <string>
#include <functional>

#include "esp_netif.h"

namespace ezdv
{

namespace network
{
    
namespace interfaces
{
    
class INetworkInterface
{
public:
    enum InterfaceStatus
    {
        INTERFACE_DOWN,
        INTERFACE_DEV_UP,
        INTERFACE_IP_UP,
        INTERFACE_SHUTTING_DOWN,
    };
    
    virtual ~INetworkInterface() = default;
    
    // Shared actions among all interface types
    virtual void bringUp() = 0;
    virtual void tearDown() = 0;
    virtual void getMacAddress(uint8_t* mac) = 0;
    void setAsDefaultInterface();
    void setHostname(std::string hostname);
    
    // Event handlers
    using OnNetworkUpDownHandlerType = std::function<void(INetworkInterface&)>;
    using OnNetworkIpAssignedType = std::function<void(INetworkInterface&, std::string)>;
    
    void setOnNetworkUp(OnNetworkUpDownHandlerType fn);
    void setOnNetworkDown(OnNetworkUpDownHandlerType fn);
    void setOnIpAddressAssigned(OnNetworkIpAssignedType fn);
    
    InterfaceStatus status() const;
    
protected:
    INetworkInterface(); // abstract class
    
    esp_netif_t* interfaceHandle_;
    esp_event_handler_instance_t ipEventHandle_;
    InterfaceStatus status_;
        
    OnNetworkUpDownHandlerType onNetworkUpFn_;
    OnNetworkUpDownHandlerType onNetworkDownFn_;
    OnNetworkIpAssignedType onIpAddressAssignedFn_;
};

}

}

}

#endif // I_NETWORK_INTERFACE_H