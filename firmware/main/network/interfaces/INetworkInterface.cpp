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

#include "INetworkInterface.h"

namespace ezdv
{

namespace network
{
    
namespace interfaces
{

INetworkInterface::INetworkInterface()
    : interfaceHandle_(nullptr)
    , ipEventHandle_(nullptr)
    , status_(INTERFACE_DOWN)
{
    // empty
}

void INetworkInterface::setAsDefaultInterface()
{
    assert(interfaceHandle_ != nullptr);
    ESP_ERROR_CHECK(esp_netif_set_default_netif(interfaceHandle_));
}

void INetworkInterface::setOnNetworkUp(OnNetworkUpDownHandlerType fn)
{
    onNetworkUpFn_ = fn;
}

void INetworkInterface::setOnNetworkDown(OnNetworkUpDownHandlerType fn)
{
    onNetworkDownFn_ = fn;
}

void INetworkInterface::setOnIpAddressAssigned(OnNetworkIpAssignedType fn)
{
    onIpAddressAssignedFn_ = fn;
}

void INetworkInterface::setHostname(std::string hostname)
{
    if (interfaceHandle_ != nullptr)
    {
        ESP_ERROR_CHECK(esp_netif_set_hostname(interfaceHandle_, hostname.c_str()));
    }
}

INetworkInterface::InterfaceStatus INetworkInterface::status() const
{
    return status_;
}

}

}

}