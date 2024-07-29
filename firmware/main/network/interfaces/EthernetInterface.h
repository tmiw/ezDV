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

#ifndef ETHERNET_INTERFACE_H
#define ETHERNET_INTERFACE_H

#include "INetworkInterface.h"

#include <string>
#include <functional>

#include "hal/eth_types.h"
#include "esp_eth.h"
#include "esp_eth_driver.h"
#include "esp_netif.h"

namespace ezdv
{

namespace network
{
    
namespace interfaces
{
    
class EthernetInterface : public INetworkInterface
{
public:
    EthernetInterface();
    virtual ~EthernetInterface();
    
    // Shared actions among all interface types
    virtual void bringUp() override;
    virtual void tearDown() override;
    virtual void getMacAddress(uint8_t* mac) override;
    
private:
    esp_event_handler_instance_t ethEventHandle_;
    esp_event_handler_instance_t ipEventHandle_;
    esp_eth_handle_t ethDeviceHandle_;
    
    static void IPEventHandler_(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
    static void EthernetEventHandler_(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
};

}

}

}

#endif // ETHERNET_INTERFACE_H