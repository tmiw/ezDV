#ifndef RADIO__ICOM__PACKET_TYPES_H
#define RADIO__ICOM__PACKET_TYPES_H

#include <string>
#include "smooth/core/network/IPacketAssembly.h"
#include "smooth/core/network/IPacketDisassembly.h"

// Thanks to the wfview project for helping reverse engineer Icom's UDP protocol.
// Original file at https://gitlab.com/eliggett/wfview/-/blob/master/packettypes.h, modified
// to take into account lower resources on the ESP32 series MCUs and available integer types.
 
namespace sm1000neo::radio::icom
{
    // See https://github.com/microenh/NetworkIcom/blob/main/Background%20Information/RS-BA1%20Analysis.txt
    // for origin of this value.
    #define MAX_PACKET_SIZE 1388
    
    #pragma pack(push, 1)

    // Various settings used by both client and server
    #define PURGE_SECONDS 1
    #define TOKEN_RENEWAL 60000
    #define PING_PERIOD 500
    #define IDLE_PERIOD 100
    #define AREYOUTHERE_PERIOD 500
    #define WATCHDOG_PERIOD 500             
    #define RETRANSMIT_PERIOD 100           // How often to attempt retransmit
    #define LOCK_PERIOD 10                  // How long to try to lock mutex (ms)
    #define STALE_CONNECTION 15             // Not heard from in this many seconds
    #define BUFSIZE 500 // Number of packets to buffer
    #define MAX_MISSING 50 // More than this indicates serious network problem 
    #define AUDIO_PERIOD 20 
    #define GUIDLEN 16


    // Fixed Size Packets
    #define CONTROL_SIZE            0x10
    #define WATCHDOG_SIZE           0x14
    #define PING_SIZE               0x15
    #define OPENCLOSE_SIZE          0x16
    #define RETRANSMIT_RANGE_SIZE   0x18
    #define TOKEN_SIZE              0x40
    #define STATUS_SIZE             0x50
    #define LOGIN_RESPONSE_SIZE     0x60
    #define LOGIN_SIZE              0x80
    #define CONNINFO_SIZE           0x90
    #define CAPABILITIES_SIZE       0x42
    #define RADIO_CAP_SIZE          0x66

    // Variable size packets + payload
    #define CIV_SIZE                0x15
    #define AUDIO_SIZE            0x18
    #define DATA_SIZE               0x15

    inline uint16_t ToBigEndian(uint16_t val)
    {
        return (val << 8) | (val >> 8);
    }
    
    inline uint16_t ToLittleEndian(uint16_t val)
    {
        return (val << 8) | (val >> 8);
    }
    
    inline uint32_t ToBigEndian(uint32_t val)
    {
        val = ((val << 8) & 0xFF00FF00) | ((val >> 8) & 0xFF00FF); 
        return (val << 16) | (val >> 16);
    }
    
    inline uint32_t ToLittleEndian(uint32_t val)
    {
        val = ((val << 8) & 0xFF00FF00) | ((val >> 8) & 0xFF00FF); 
        return (val << 16) | (val >> 16);
    }
    
    // 0x10 length control packet (connect/disconnect/idle.)
    typedef union control_packet {
        struct {
            uint32_t len;
            uint16_t type;
            uint16_t seq;
            uint32_t sentid;
            uint32_t rcvdid;
        };
        char packet[CONTROL_SIZE];
    } *control_packet_t;


    // 0x14 length watchdog packet
    typedef union watchdog_packet {
        struct {
            uint32_t len;        // 0x00
            uint16_t type;       // 0x04
            uint16_t seq;        // 0x06
            uint32_t sentid;     // 0x08
            uint32_t rcvdid;     // 0x0c
            uint16_t  secondsa;        // 0x10
            uint16_t  secondsb;        // 0x12
        };
        char packet[WATCHDOG_SIZE];
    } *watchdog_packet_t;


    // 0x15 length ping packet 
    // Also used for the slightly different civ header packet.
    typedef union ping_packet {
        struct
        {
            uint32_t len;        // 0x00
            uint16_t type;       // 0x04
            uint16_t seq;        // 0x06
            uint32_t sentid;     // 0x08
            uint32_t rcvdid;     // 0x0c
            char  reply;        // 0x10
            union { // This contains differences between the send/receive packet
                struct { // Ping
                    uint32_t time;      // 0x11
                };
                struct { // Send
                    uint16_t datalen;    // 0x11
                    uint16_t sendseq;    //0x13
                };
            };

        };
        char packet[PING_SIZE];
    } *ping_packet_t, * data_packet_t, data_packet;

    // 0x16 length open/close packet
    typedef union openclose_packet {
        struct
        {
            uint32_t len;        // 0x00
            uint16_t type;       // 0x04
            uint16_t seq;        // 0x06
            uint32_t sentid;     // 0x08
            uint32_t rcvdid;     // 0x0c
            uint16_t data;       // 0x10
            char unused;        // 0x11
            uint16_t sendseq;    //0x13
            char magic;         // 0x15

        };
        char packet[OPENCLOSE_SIZE];
    } *startstop_packet_t;


    // 0x18 length audio packet 
    typedef union audio_packet {
        struct
        {
            uint32_t len;        // 0x00
            uint16_t type;       // 0x04
            uint16_t seq;        // 0x06
            uint32_t sentid;     // 0x08
            uint32_t rcvdid;     // 0x0c

            uint16_t ident;      // 0x10
            uint16_t sendseq;    // 0x12
            uint16_t unused;     // 0x14
            uint16_t datalen;    // 0x16
        };
        char packet[AUDIO_SIZE];
    } *audio_packet_t;

    // 0x18 length retransmit_range packet 
    typedef union retransmit_range_packet {
        struct
        {
            uint32_t len;        // 0x00
            uint16_t type;       // 0x04
            uint16_t seq;        // 0x06
            uint32_t sentid;     // 0x08
            uint32_t rcvdid;     // 0x0c
            uint16_t first;      // 0x10
            uint16_t second;        // 0x12
            uint16_t third;      // 0x14
            uint16_t fourth;        // 0x16
        };
        char packet[RETRANSMIT_RANGE_SIZE];
    } *retransmit_range_packet_t;


    // 0x18 length txaudio packet 
    /*            tx[0] = static_cast<uint8_t>(tx.length() & 0xff);
                tx[1] = static_cast<uint8_t>(tx.length() >> 8 & 0xff);
                tx[18] = static_cast<uint8_t>(sendAudioSeq >> 8 & 0xff);
                tx[19] = static_cast<uint8_t>(sendAudioSeq & 0xff);
                tx[22] = static_cast<uint8_t>(partial.length() >> 8 & 0xff);
                tx[23] = static_cast<uint8_t>(partial.length() & 0xff);*/


    // 0x40 length token packet
    typedef union token_packet {
        struct
        {
            uint32_t len;                // 0x00
            uint16_t type;               // 0x04
            uint16_t seq;                // 0x06
            uint32_t sentid;             // 0x08 
            uint32_t rcvdid;             // 0x0c
            char unuseda[2];          // 0x10
            uint16_t payloadsize;      // 0x12
            uint8_t requestreply;      // 0x13
            uint8_t requesttype;       // 0x14
            uint16_t innerseq;         // 0x16
            char unusedb[2];          // 0x18
            uint16_t tokrequest;         // 0x1a
            uint32_t token;              // 0x1c
            union {
                struct {
                    uint16_t authstartid;    // 0x20
                    char unusedg[5];        // 0x22
                    uint16_t commoncap;      // 0x27
                    char unusedh;           // 0x29
                    uint8_t macaddress[6];     // 0x2a
                };
                uint8_t guid[GUIDLEN];                  // 0x20
            };
            uint32_t response;           // 0x30
            char unusede[12];           // 0x34
        };
        char packet[TOKEN_SIZE];
    } *token_packet_t;

    // 0x50 length login status packet
    typedef union status_packet {
        struct
        {
            uint32_t len;                // 0x00
            uint16_t type;               // 0x04
            uint16_t seq;                // 0x06
            uint32_t sentid;             // 0x08 
            uint32_t rcvdid;             // 0x0c
            char unuseda[2];          // 0x10
            uint16_t payloadsize;      // 0x12
            uint8_t requestreply;      // 0x13
            uint8_t requesttype;       // 0x14
            uint16_t innerseq;         // 0x16
            char unusedb[2];          // 0x18
            uint16_t tokrequest;         // 0x1a
            uint32_t token;              // 0x1c 
            union {
                struct {
                    uint16_t authstartid;    // 0x20
                    char unusedd[5];        // 0x22
                    uint16_t commoncap;      // 0x27
                    char unusede;           // 0x29
                    uint8_t macaddress[6];     // 0x2a
                };
                uint8_t guid[GUIDLEN];                  // 0x20
            };
            uint32_t error;             // 0x30
            char unusedg[12];         // 0x34
            char disc;                // 0x40
            char unusedh;             // 0x41
            uint16_t civport;          // 0x42 // Sent bigendian
            uint16_t unusedi;          // 0x44 // Sent bigendian
            uint16_t audioport;        // 0x46 // Sent bigendian
            char unusedj[7];          // 0x49
        };
        char packet[STATUS_SIZE];
    } *status_packet_t;

    // 0x60 length login status packet
    typedef union login_response_packet {
        struct
        {
            uint32_t len;                // 0x00
            uint16_t type;               // 0x04
            uint16_t seq;                // 0x06
            uint32_t sentid;             // 0x08 
            uint32_t rcvdid;             // 0x0c
            char unuseda[2];          // 0x10
            uint16_t payloadsize;      // 0x12
            uint8_t requestreply;      // 0x13
            uint8_t requesttype;       // 0x14
            uint16_t innerseq;         // 0x16
            char unusedb[2];          // 0x18
            uint16_t tokrequest;         // 0x1a
            uint32_t token;              // 0x1c 
            uint16_t authstartid;        // 0x20
            char unusedd[14];           // 0x22
            uint32_t error;              // 0x30
            char unusede[12];           // 0x34
            char connection[16];        // 0x40
            char unusedf[16];           // 0x50
        };
        char packet[LOGIN_RESPONSE_SIZE];
    } *login_response_packet_t;

    // 0x80 length login packet
    typedef union login_packet {
        struct
        {
            uint32_t len;                // 0x00
            uint16_t type;               // 0x04
            uint16_t seq;                // 0x06
            uint32_t sentid;             // 0x08 
            uint32_t rcvdid;             // 0x0c
            char unuseda[2];            // 0x10
            uint16_t payloadsize;        // 0x12
            uint8_t requestreply;      // 0x13
            uint8_t requesttype;       // 0x14
            uint16_t innerseq;           // 0x16
            char unusedb[2];            // 0x18
            uint16_t tokrequest;         // 0x1a
            uint32_t token;              // 0x1c 
            char unusedc[32];           // 0x20
            char username[16];          // 0x40
            char password[16];          // 0x50
            char name[16];              // 0x60
            char unusedf[16];           // 0x70
        };
        char packet[LOGIN_SIZE];
    } *login_packet_t;

    // 0x90 length conninfo and stream request packet
    typedef union conninfo_packet {
        struct
        {
            uint32_t len;              // 0x00
            uint16_t type;             // 0x04
            uint16_t seq;              // 0x06
            uint32_t sentid;           // 0x08 
            uint32_t rcvdid;           // 0x0c
            char unuseda[2];          // 0x10
            uint16_t payloadsize;      // 0x12
            uint8_t requestreply;      // 0x13
            uint8_t requesttype;       // 0x14
            uint16_t innerseq;         // 0x16
            char unusedb[2];          // 0x18
            uint16_t tokrequest;       // 0x1a
            uint32_t token;            // 0x1c 
            union {
                struct {
                    uint16_t authstartid;    // 0x20
                    char unusedg[5];        // 0x22
                    uint16_t commoncap;      // 0x27
                    char unusedh;           // 0x29
                    uint8_t macaddress[6];     // 0x2a
                };
                uint8_t guid[GUIDLEN];                  // 0x20
            };
            char unusedab[16];        // 0x30
            char name[32];                  // 0x40
            union { // This contains differences between the send/receive packet
                struct { // Receive
                    uint32_t busy;            // 0x60
                    char computer[16];        // 0x64
                    char unusedi[16];         // 0x74
                    uint32_t ipaddress;        // 0x84
                    char unusedj[8];          // 0x78
                };
                struct { // Send
                    char username[16];    // 0x60 
                    char rxenable;        // 0x70
                    char txenable;        // 0x71
                    char rxcodec;         // 0x72
                    char txcodec;         // 0x73
                    uint32_t rxsample;     // 0x74
                    uint32_t txsample;     // 0x78
                    uint32_t civport;      // 0x7c
                    uint32_t audioport;    // 0x80
                    uint32_t txbuffer;     // 0x84
                    uint8_t convert;      // 0x88
                    char unusedl[7];      // 0x89
                };
            };
        };
        char packet[CONNINFO_SIZE];
    } *conninfo_packet_t;


    // 0x64 length radio capabilities part of cap packet.

    typedef union radio_cap_packet {
        struct
        {
            union {
                struct {
                    uint8_t unusede[7];          // 0x00
                    uint16_t commoncap;          // 0x07
                    uint8_t unused;              // 0x09
                    uint8_t macaddress[6];       // 0x0a
                };
                uint8_t guid[GUIDLEN];           // 0x0
            };
            char name[32];            // 0x10
            char audio[32];           // 0x30
            uint16_t conntype;         // 0x50
            char civ;                 // 0x52
            uint16_t rxsample;         // 0x53
            uint16_t txsample;         // 0x55
            uint8_t enablea;           // 0x57
            uint8_t enableb;           // 0x58
            uint8_t enablec;           // 0x59
            uint32_t baudrate;         // 0x5a
            uint16_t capf;             // 0x5e
            char unusedi;             // 0x60
            uint16_t capg;             // 0x61
            char unusedj[3];          // 0x63
        };
        char packet[RADIO_CAP_SIZE];
    } *radio_cap_packet_t;



    // 0xA8 length capabilities packet
    typedef union capabilities_packet {
        struct
        {
            uint32_t len;              // 0x00
            uint16_t type;             // 0x04
            uint16_t seq;              // 0x06
            uint32_t sentid;           // 0x08 
            uint32_t rcvdid;           // 0x0c
            char unuseda[2];          // 0x10
            uint16_t payloadsize;      // 0x12
            uint8_t requestreply;      // 0x13
            uint8_t requesttype;       // 0x14
            uint16_t innerseq;         // 0x16
            char unusedb[2];          // 0x18
            uint16_t tokrequest;       // 0x1a
            uint32_t token;            // 0x1c 
            char unusedd[32];         // 0x20
            uint16_t numradios;        // 0x40
        };
        char packet[CAPABILITIES_SIZE];
    } *capabilities_packet_t;

#pragma pack(pop)

    // The below definitions are needed by Smooth's socket handling. They don't do much as
    // we just want them to copy a single UDP datagram directly into one of the above packets.
    
    class IcomProtocol;
    
    class IcomPacket
        : public smooth::core::network::IPacketDisassembly
    {
    public:
        friend IcomProtocol;
        
        IcomPacket();
        IcomPacket(char* existingPacket, int size);
        IcomPacket(int size);
        IcomPacket(const IcomPacket& packet);
        IcomPacket(IcomPacket&& packet);
        virtual ~IcomPacket();
        
        virtual int get_send_length();
        virtual const uint8_t* get_data();
        
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
        
        // Used in Are You Here state for checking for I Am Here response
        bool isIAmHere(uint32_t& theirId);
        
        // Used in Are You Ready state for checking I Am Ready response
        bool isIAmReady();
        
        // Used in Login state to get login response
        bool isLoginResponse(std::string& connectionType, bool& isInvalidPassword, uint16_t& tokenRequest, uint32_t& radioToken);
        
        // Used in Login state to check for ping requests and responses
        bool isPingRequest(uint16_t& pingSequence);
        bool isPingResponse(uint16_t& pingSequence);
        
    private:
        char* rawPacket_;
        int size_;
        
        static void EncodePassword_(std::string str, char* output);
    };
    
    class IcomProtocol 
        : public smooth::core::network::IPacketAssembly<IcomProtocol, IcomPacket>
    {
    public:
        using packet_type = IcomPacket;
        
        IcomProtocol();
        virtual ~IcomProtocol() = default;
        
        virtual int get_wanted_amount(IcomPacket& packet);
        virtual void data_received(IcomPacket& packet, int length);
        virtual uint8_t* get_write_pos(IcomPacket& packet);
        virtual bool is_complete(IcomPacket& packet) const;
        virtual bool is_error();
        virtual void packet_consumed();
        virtual void reset();
        
    private:
        int amountRead_;
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

#endif // RADIO__ICOM__PACKET_TYPES_H
