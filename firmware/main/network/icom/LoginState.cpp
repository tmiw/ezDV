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

#include <sys/socket.h>

#include "esp_log.h"
#include "LoginState.h"
#include "IcomStateMachine.h"
#include "RadioPacketDefinitions.h"
#include "IcomMessage.h"

namespace ezdv
{

namespace network
{

namespace icom
{

LoginState::LoginState(IcomStateMachine* parent)
    : TrackedPacketState(parent)
    , tokenRenewTimer_(parent->getTask(), std::bind(&LoginState::onTokenRenewTimer_, this), MS_TO_US(TOKEN_RENEWAL))
    , ourTokenRequest_(0)
    , theirToken_(0)
    , authSequenceNumber_(0)
    , civPort_(0)
    , audioPort_(0)
    , isDisconnecting_(false)
{
    // empty
}

void LoginState::onEnterState()
{
    TrackedPacketState::onEnterState();

    // Reset token/auth info
    isDisconnecting_ = false;
    ourTokenRequest_ = 0;
    theirToken_ = 0;
    authSequenceNumber_ = 0;
    civPort_ = 0;
    audioPort_ = 0;

    // Login packet is only necessary on the control state machine.
    sendLoginPacket_();
    clearRadioCapabilities_();
}

void LoginState::onExitState()
{
    // Disable token renew timer.
    tokenRenewTimer_.stop();
    
    // Send token removal packet to cause radio to disconnect.
    sendTokenRemovePacket_();

    // Perform base class cleanup actions.
    TrackedPacketState::onExitState();
}

std::string LoginState::getName()
{
    return "Login";
}

void LoginState::onReceivePacket(IcomPacket& packet)
{
    std::string connType;
    bool packetSent = false;
    bool isPasswordIncorrect;
    uint16_t tokenRequest;
    uint32_t radioToken;
    std::vector<radio_cap_packet_t> radios;
    std::string radioName;
    uint32_t radioIp;
    bool isBusy;
    bool connSuccess;
    bool connDisconnected;
    uint16_t remoteCivPort;
    uint16_t remoteAudioPort;

    if (packet.isLoginResponse(connType, isPasswordIncorrect, tokenRequest, radioToken))
    {
        ESP_LOGI(parent_->getName().c_str(), "Connection type: %s", connType.c_str());
        ESP_LOGI(parent_->getName().c_str(), "Password incorrect: %d", isPasswordIncorrect);
        ESP_LOGI(parent_->getName().c_str(), "Token req: %x, our token req: %lx, radio token: %lx", tokenRequest, ourTokenRequest_, radioToken);
        
        if (!isPasswordIncorrect && tokenRequest == ourTokenRequest_)
        {
            ESP_LOGI(parent_->getName().c_str(), "Login successful, acknowledging token");
            sendTokenAckPacket_(radioToken);
            packetSent = true;
            
            // Begin renewing token every 60 seconds.
            tokenRenewTimer_.start();
        }
        else
        {
            ESP_LOGE(parent_->getName().c_str(), "Password incorrect!");

            // TBD: cleanup?
        }
    }
    else if (packet.isCapabilitiesPacket(radios))
    {
        ESP_LOGI(parent_->getName().c_str(), "Available radios:");
        int index = 0;
        for (auto& radio : radios)
        {
            ESP_LOGI(
                parent_->getName().c_str(),
                "[%d]    %s: MAC=%02x:%02x:%02x:%02x:%02x:%02x, CIV=%02x, Audio=%s (rxsample %d, txsample %d)", 
                index++, 
                radio->name,
                radio->macaddress[0], radio->macaddress[1], radio->macaddress[2], radio->macaddress[3], radio->macaddress[4], radio->macaddress[5],
                radio->civ, 
                radio->audio,
                radio->rxsample,
                radio->txsample);
                
            insertCapability_(radio);
        }
    }
    else if (packet.isConnInfoPacket(radioName, radioIp, isBusy) && !isDisconnecting_)
    {
        ESP_LOGI(
            parent_->getName().c_str(), 
            "Connection info for %s: IP = %lx, Is Busy = %d",
            radioName.c_str(),
            radioIp,
            isBusy ? 1 : 0);
            
        sendUseRadioPacket_(0);
        packetSent = true;
    }
    else if (packet.isStatusPacket(connSuccess, connDisconnected, remoteCivPort, remoteAudioPort))
    {
        if (connSuccess)
        {
            if (remoteAudioPort == 0 && remoteCivPort == 0)
            {
                // Radio is shutting down.
                ESP_LOGI(parent_->getName().c_str(), "Radio is shutting down!");
                isDisconnecting_ = true;

                IcomCIVAudioConnectionInfo message(0, 0, 0, 0);
                parent_->getTask()->publish(&message);

                parent_->reset();
            }
            else
            {
                ESP_LOGI(
                    parent_->getName().c_str(), 
                    "Starting audio and CIV state machines using remote ports %d and %d",
                    remoteAudioPort,
                    remoteCivPort);
                
                IcomCIVAudioConnectionInfo message(civPort_, remoteCivPort, audioPort_, remoteAudioPort);
                parent_->getTask()->publish(&message);
            }
        }
        else if (connDisconnected)
        {
            ESP_LOGE(
                parent_->getName().c_str(), 
                "Disconnected from the radio"
            );
            
            parent_->transitionState(IcomProtocolState::ARE_YOU_THERE);
        }
        else
        {
            ESP_LOGE(
                parent_->getName().c_str(), 
                "Connection failed"
            );
                
            parent_->transitionState(IcomProtocolState::ARE_YOU_THERE);
        }
    }
    
    // Call into parent to perform missing packet handling.
    TrackedPacketState::onReceivePacket(packet);

    if (packetSent)
    {
        idleTimer_.stop();
        idleTimer_.start();
    }
}

void LoginState::sendLoginPacket_()
{
    auto packet = IcomPacket::CreateLoginPacket(authSequenceNumber_++, parent_->getOurIdentifier(), parent_->getTheirIdentifier(), parent_->getUsername(), parent_->getPassword(), "ezdv");
    auto typedPacket = packet.getConstTypedPacket<login_packet>();
    
    ourTokenRequest_ = typedPacket->tokrequest;
    sendTracked_(packet);
}

void LoginState::sendTokenAckPacket_(uint32_t theirToken)
{
    theirToken_ = theirToken;
    
    auto packet = IcomPacket::CreateTokenAckPacket(authSequenceNumber_++, ourTokenRequest_, theirToken, parent_->getOurIdentifier(), parent_->getTheirIdentifier());
    sendTracked_(packet);
}

void LoginState::sendTokenRenewPacket_()
{
    auto packet = IcomPacket::CreateTokenRenewPacket(authSequenceNumber_++, ourTokenRequest_, theirToken_, parent_->getOurIdentifier(), parent_->getTheirIdentifier());
    sendTracked_(packet);
}

void LoginState::sendTokenRemovePacket_()
{
    auto packet = IcomPacket::CreateTokenRemovePacket(authSequenceNumber_++, ourTokenRequest_, theirToken_, parent_->getOurIdentifier(), parent_->getTheirIdentifier());
    sendTracked_(packet);
}

void LoginState::onTokenRenewTimer_()
{
    // Token renewal time
    ESP_LOGI(parent_->getName().c_str(), "Renewing token");
    sendTokenRenewPacket_();
}

void LoginState::clearRadioCapabilities_()
{
    radioCapabilities_.clear();
}

void LoginState::insertCapability_(radio_cap_packet_t radio)
{
    IcomPacket packet((char*)&radio, sizeof(radio_cap_packet));
    //radioCapabilities_.push_back(std::move(packet));
    radioCapabilities_.push_back(packet);
}

void LoginState::sendUseRadioPacket_(int radioIndex)
{
    if (civPort_ > 0 || audioPort_ > 0)
    {
        // we've already connected, no need to try again
        return;
    }
    
    // We need to create CIV and audio sockets and get their local port numbers.
    // The protocol seems to require it, which is weird but ok.
    auto civSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    auto audioSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    assert(civSocket > 0 && audioSocket > 0); // TBD -- should just force a disconnect of the main SM instead.
    
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = 0;
    auto rv = bind(civSocket, (struct sockaddr *) &sin, sizeof sin);
    assert(rv >= 0);
    
    socklen_t addressLength = sizeof(sin);
    getsockname(civSocket, (struct sockaddr*)&sin, &addressLength);
    civPort_ = ntohs(sin.sin_port);
    
    ESP_LOGI(parent_->getName().c_str(), "Local UDP port for CIV comms will be %d", civPort_);
    
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = 0;
    rv = bind(audioSocket, (struct sockaddr *) &sin, sizeof sin);
    assert(rv >= 0);
    
    addressLength = sizeof(sin);
    getsockname(audioSocket, (struct sockaddr*)&sin, &addressLength);
    audioPort_ = ntohs(sin.sin_port);
    
    ESP_LOGI(parent_->getName().c_str(), "Local UDP port for audio comms will be %d", audioPort_);
    
    close(civSocket);
    close(audioSocket);

    IcomPacket packet(sizeof(conninfo_packet));
    auto typedPacket = packet.getTypedPacket<conninfo_packet>();
    
    typedPacket->len = sizeof(conninfo_packet);
    typedPacket->sentid = parent_->getOurIdentifier();
    typedPacket->rcvdid = parent_->getTheirIdentifier();
    typedPacket->payloadsize = ToBigEndian((uint16_t)(sizeof(conninfo_packet) - 0x10));
    typedPacket->requesttype = 0x03;
    typedPacket->requestreply = 0x01;
    
    IcomPacket& capPacket = radioCapabilities_[radioIndex];
    auto cap = capPacket.getConstTypedPacket<radio_cap_packet>();
    if (cap->commoncap == 0x8010)
    {
        // can use MAC address in packet
        typedPacket->commoncap = 0x8010;
        memcpy(&typedPacket->macaddress, cap->macaddress, 6);
    }
    else
    {
        memcpy(&typedPacket->guid, cap->guid, GUIDLEN);
    }
    
    typedPacket->innerseq = ToBigEndian(authSequenceNumber_++);
    typedPacket->tokrequest = ourTokenRequest_;
    typedPacket->token = theirToken_;
    memcpy(typedPacket->name, cap->name, strlen(cap->name));
    typedPacket->rxenable = 1;
    typedPacket->txenable = 1;
    
    // Force 48K sample rate PCM
    typedPacket->rxcodec = 4;
    typedPacket->rxsample = ToBigEndian((uint32_t)48000);
    typedPacket->txcodec = 4;
    typedPacket->txsample = ToBigEndian((uint32_t)48000);
    
    // CIV/audio local port numbers and latency
    typedPacket->civport = ToBigEndian((uint32_t)civPort_);
    typedPacket->audioport = ToBigEndian((uint32_t)audioPort_);
    typedPacket->txbuffer = ToBigEndian((uint32_t)160); // 16 packet buffer at IC-705 (480 samples/packet @ 48K)
    typedPacket->convert = 1;
    
    sendTracked_(packet);
}

}

}

}