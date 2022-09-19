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

#ifndef ICOM_PACKET_H
#define ICOM_PACKET_H

#include "esp_heap_caps.h"

#include <string>
#include <vector>
#include <memory>
#include "RadioPacketDefinitions.h"

namespace ezdv
{

namespace network
{

namespace icom
{

template<typename T>
struct IcomAllocator : public std::allocator<T>
{
    typename std::allocator<T>::pointer allocate( typename std::allocator<T>::size_type n, const void * hint = 0 )
    {
        return (typename std::allocator<T>::pointer)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
    }

    void deallocate( T* p, std::size_t n )
    {
        heap_caps_free(p);
    }
};

class IcomPacket
{
public:
    // To force allocation inside SPI RAM.
    static void* operator new(std::size_t count);
    static void operator delete(void* ptr);

    IcomPacket();
    IcomPacket(char* existingPacket, int size);
    IcomPacket(int size);
    IcomPacket(const IcomPacket& packet);
    IcomPacket(IcomPacket&& packet);
    virtual ~IcomPacket();
    
    virtual int getSendLength();
    virtual const uint8_t* getData();
    
    template<typename ActualPacketType>
    ActualPacketType* getTypedPacket();
    
    template<typename ActualPacketType>
    const ActualPacketType* getConstTypedPacket();
    
    IcomPacket& operator=(const IcomPacket& packet);
    IcomPacket& operator=(IcomPacket&& packet);
    
    static IcomPacket CreateAreYouTherePacket(uint32_t ourId, uint32_t theirId);
    static IcomPacket CreateAreYouReadyPacket(uint32_t ourId, uint32_t theirId);
    static IcomPacket CreateLoginPacket(uint16_t authSeq, uint32_t ourId, uint32_t theirId, std::string username, std::string password, std::string computerName);
    static IcomPacket CreateTokenAckPacket(uint16_t authSeq, uint16_t tokenRequest, uint32_t token, uint32_t ourId, uint32_t theirId);
    static IcomPacket CreatePingPacket(uint16_t pingSeq, uint32_t ourId, uint32_t theirId);
    static IcomPacket CreatePingAckPacket(uint16_t theirPingSeq, uint32_t ourId, uint32_t theirId);
    static IcomPacket CreateIdlePacket(uint16_t ourSeq, uint32_t ourId, uint32_t theirId);
    static IcomPacket CreateRetransmitRequest(uint32_t ourId, uint32_t theirId, std::vector<uint16_t> packetIdsToRetransmit);
    static IcomPacket CreateTokenRenewPacket(uint16_t authSeq, uint16_t tokenRequest, uint32_t token, uint32_t ourId, uint32_t theirId);
    static IcomPacket CreateTokenRemovePacket(uint16_t authSeq, uint16_t tokenRequest, uint32_t token, uint32_t ourId, uint32_t theirId);
    static IcomPacket CreateDisconnectPacket(uint32_t ourId, uint32_t theirId);
    static IcomPacket CreateCIVPacket(uint32_t ourId, uint32_t theirId, uint16_t sendSeq, uint8_t* civData, uint16_t civLength);
    static IcomPacket CreateCIVOpenClosePacket(uint16_t civSeq, uint32_t ourId, uint32_t theirId, bool close);
    static IcomPacket CreateAudioPacket(uint16_t audioSeq, uint32_t ourId, uint32_t theirId, short* audio, uint16_t len);
    
    // Used in Are You Here state for checking for I Am Here response
    bool isIAmHere(uint32_t& theirId);
    
    // Used in Are You Ready state for checking I Am Ready response
    bool isIAmReady();
    
    // Used in Login state to get login response
    bool isLoginResponse(std::string& connectionType, bool& isInvalidPassword, uint16_t& tokenRequest, uint32_t& radioToken);
    
    // Used in Login state to check for ping requests and responses
    bool isPingRequest(uint16_t& pingSequence);
    bool isPingResponse(uint16_t& pingSequence);
    
    bool isCapabilitiesPacket(std::vector<radio_cap_packet_t>& radios);
    
    bool isRetransmitPacket(std::vector<uint16_t>& retryPackets);
    
    bool isConnInfoPacket(std::string& name, uint32_t& ip, bool& isBusy);
    
    bool isStatusPacket(bool& connSuccessful, bool& disconnected, uint16_t& civPort, uint16_t& audioPort);
    
    bool isAudioPacket(uint16_t& seqId, short** dataStart);
    
    bool isCivPacket(uint8_t** civPacket, uint16_t* civPacketLength);
    
private:
    char* rawPacket_;
    int size_;
    
    static void EncodePassword_(std::string str, char* output);
};

template<typename ActualPacketType>
ActualPacketType* IcomPacket::getTypedPacket()
{
    return (ActualPacketType*)rawPacket_;
}

template<typename ActualPacketType>
const ActualPacketType* IcomPacket::getConstTypedPacket()
{
    return (const ActualPacketType*)rawPacket_;
}

}
    
}

}

#endif // RADIO_PACKET_DEFINITIONS_H