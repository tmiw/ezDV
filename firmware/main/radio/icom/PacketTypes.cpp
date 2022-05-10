#include <utility>
#include <cassert>
#include <cstring>
#include <random>
#include "PacketTypes.h"

namespace sm1000neo::radio::icom
{
    IcomPacket::IcomPacket()
        : rawPacket_(new char[MAX_PACKET_SIZE])
        , size_(MAX_PACKET_SIZE)
    {
        assert(rawPacket_ != nullptr);
        memset(rawPacket_, 0, MAX_PACKET_SIZE);
    }
    
    IcomPacket::IcomPacket(char* existingPacket, int size)
        : rawPacket_(existingPacket)
        , size_(size)
    {
        assert(rawPacket_ != nullptr);
    }
    
    IcomPacket::IcomPacket(int size)
        : rawPacket_(new char[size])
        , size_(size)
    {
        assert(rawPacket_ != nullptr);
        memset(rawPacket_, 0, size);
    }
        
    IcomPacket::IcomPacket(const IcomPacket& packet)
        : rawPacket_(new char[packet.size_])
        , size_(packet.size_)
    {
        assert(rawPacket_ != nullptr);
        memcpy(rawPacket_, packet.rawPacket_, size_);
    }
    
    IcomPacket::IcomPacket(IcomPacket&& packet)
        : rawPacket_(std::move(packet.rawPacket_))
        , size_(packet.size_)
    {
        packet.rawPacket_ = nullptr;
        packet.size_ = 0;
    }
    
    IcomPacket::~IcomPacket()
    {
        delete[] rawPacket_;
    }
    
    int IcomPacket::get_send_length()
    {
        return size_;
    }
    
    const uint8_t* IcomPacket::get_data()
    {
        return (const uint8_t*)rawPacket_;
    }
    
    IcomPacket& IcomPacket::operator=(const IcomPacket& packet)
    {
        delete[] rawPacket_;
        
        rawPacket_ = new char[packet.size_];
        assert(rawPacket_ != nullptr);
        memcpy(rawPacket_, packet.rawPacket_, packet.size_);
        size_ = packet.size_;
        
        return *this;
    }
    
    IcomPacket& IcomPacket::operator=(IcomPacket&& packet)
    {
        delete[] rawPacket_;
        
        rawPacket_ = std::move(packet.rawPacket_);
        size_ = packet.size_;
        packet.rawPacket_ = nullptr;
        packet.size_ = 0;
        
        return *this;
    }
    
    IcomProtocol::IcomProtocol()
        : amountRead_(0)
    {
        // empty
    }
    
    int IcomProtocol::get_wanted_amount(IcomPacket& packet)
    {
        if (*(uint32_t*)packet.rawPacket_ == 0)
        {
            // Should return MAX_PACKET_SIZE if we haven't read anything yet.
            return packet.size_;
        }
        
        // Otherwise, as UDP returns discrete datagrams, we shouldn't need to
        // read any more for this packet.
        return 0;
    }
    
    void IcomProtocol::data_received(IcomPacket& packet, int length)
    {
        amountRead_ += length;
        
        // To reduce the amount of RAM we're using, we create
        // a new packet of length length and copy everything over.
        char* tmp = new char[length];
        assert(tmp != nullptr);
        memcpy(tmp, packet.rawPacket_, length);
        
        IcomPacket tmpPacket(tmp, length);
        packet = std::move(tmpPacket);
    }
    
    uint8_t* IcomProtocol::get_write_pos(IcomPacket& packet)
    {
        return (uint8_t*)packet.rawPacket_;
    }
    
    bool IcomProtocol::is_complete(IcomPacket& packet) const
    {
        // We're complete if we've read anything at all.
        return !(*(uint32_t*)packet.rawPacket_ == 0);
    }
    
    bool IcomProtocol::is_error()
    {
        return false;
    }
    
    void IcomProtocol::packet_consumed()
    {
        // empty
    }
    
    void IcomProtocol::reset()
    {
        amountRead_ = 0;
    }
    
    IcomPacket IcomPacket::CreateAreYouTherePacket(uint32_t ourId, uint32_t theirId)
    {
        constexpr uint16_t packetType = 0x03;
        
        IcomPacket result(sizeof(control_packet));
        auto packet = result.getTypedPacket<control_packet>();
        packet->len = sizeof(control_packet);
        packet->type = packetType;
        packet->seq = 0; // always the first packet, so no need for a sequence number
        packet->sentid = ourId;
        packet->rcvdid = theirId;
        
        return result;
    }
    
    IcomPacket IcomPacket::CreateAreYouReadyPacket(uint32_t ourId, uint32_t theirId)
    {
        constexpr uint16_t packetType = 0x06;
        
        IcomPacket result(sizeof(control_packet));
        auto packet = result.getTypedPacket<control_packet>();
        packet->len = sizeof(control_packet);
        packet->type = packetType;
        packet->seq = 1; // always the second packet, so no need for a sequence number
        packet->sentid = ourId;
        packet->rcvdid = theirId;
        
        return result;
    }
    
    IcomPacket IcomPacket::CreateLoginPacket(uint16_t authSeq, uint32_t ourId, uint32_t theirId, std::string username, std::string password, std::string computerName)
    {
        // Generate random token
        std::random_device r;
        std::default_random_engine generator(r());
        std::uniform_int_distribution<uint16_t> uniform_dist(0, UINT16_MAX);
        uint16_t tokRequest = uniform_dist(generator);
        
        IcomPacket result(sizeof(login_packet));
        auto packet = result.getTypedPacket<login_packet>();
        packet->len = sizeof(login_packet);
        packet->sentid = ourId;
        packet->rcvdid = theirId;
        packet->payloadsize = ToBigEndian((uint16_t)(sizeof(login_packet) - 0x10)); // ?? why subtract 16?
        packet->requesttype = 0x00;
        packet->requestreply = 0x01;
        packet->innerseq = ToBigEndian(authSeq);
        packet->tokrequest = tokRequest;
        
        EncodePassword_(username, packet->username);
        EncodePassword_(password, packet->password);
        memcpy(packet->name, computerName.c_str(), computerName.size());
        
        return result;
    }
    
    IcomPacket IcomPacket::CreateTokenAckPacket(uint16_t authSeq, uint16_t tokenRequest, uint32_t token, uint32_t ourId, uint32_t theirId)
    {
        IcomPacket result(sizeof(token_packet));
        auto packet = result.getTypedPacket<token_packet>();
        packet->len = sizeof(login_packet);
        
        packet->sentid = ourId;
        packet->rcvdid = theirId;
        packet->payloadsize = ToBigEndian((uint16_t)(sizeof(token_packet) - 0x10));
        packet->requesttype = 0x02;
        packet->requestreply = 0x01;
        packet->innerseq = ToBigEndian(authSeq);
        packet->tokrequest = tokenRequest;
        packet->token = token;

        return result;
    }
    
    IcomPacket IcomPacket::CreatePingPacket(uint16_t pingSeq, uint32_t ourId, uint32_t theirId)
    {
        constexpr uint16_t packetType = 0x07;
        
        IcomPacket result(sizeof(ping_packet));
        auto packet = result.getTypedPacket<ping_packet>();
        packet->len = sizeof(control_packet);
        packet->type = packetType;
        packet->seq = pingSeq;
        packet->sentid = ourId;
        packet->rcvdid = theirId;
        packet->time = time(NULL); // wfview used milliseconds since start of day, not sure that matters
        return result;
    }
    
    void IcomPacket::EncodePassword_(std::string str, char* output)
    {
        const uint8_t sequence[] =
        {
            0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
            0x47,0x5d,0x4c,0x42,0x66,0x20,0x23,0x46,0x4e,0x57,0x45,0x3d,0x67,0x76,0x60,0x41,0x62,0x39,0x59,0x2d,0x68,0x7e,
            0x7c,0x65,0x7d,0x49,0x29,0x72,0x73,0x78,0x21,0x6e,0x5a,0x5e,0x4a,0x3e,0x71,0x2c,0x2a,0x54,0x3c,0x3a,0x63,0x4f,
            0x43,0x75,0x27,0x79,0x5b,0x35,0x70,0x48,0x6b,0x56,0x6f,0x34,0x32,0x6c,0x30,0x61,0x6d,0x7b,0x2f,0x4b,0x64,0x38,
            0x2b,0x2e,0x50,0x40,0x3f,0x55,0x33,0x37,0x25,0x77,0x24,0x26,0x74,0x6a,0x28,0x53,0x4d,0x69,0x22,0x5c,0x44,0x31,
            0x36,0x58,0x3b,0x7a,0x51,0x5f,0x52,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0

        };

        uint8_t* ascii = (uint8_t*)str.c_str();
        for (int i = 0; i < str.size() && i < 16; i++)
        {
            int p = ascii[i] + i;
            if (p > 126) 
            {
                p = 32 + p % 127;
            }
            
            *output++ = (char)sequence[p];
        }
    }
}