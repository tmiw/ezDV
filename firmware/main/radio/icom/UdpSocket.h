#ifndef ICOM__UDP_SOCKET_H
#define ICOM__UDP_SOCKET_H

#include "smooth/core/network/Socket.h"

namespace sm1000neo::radio::icom
{
    // This is an override of Smooth's Socket class that creates the socket as UDP.
    // Smooth by default only creates TCP sockets. Based on the Smooth source code,
    // it should be okay just to override a few methods to ensure we create sockets
    // as SOCK_DGRAM and not set TCP_NODELAY.
    template<typename Protocol, typename Packet = typename Protocol::packet_type>
    class UdpSocket : public smooth::core::network::Socket<Protocol, Packet>
    {
    public:
        static std::shared_ptr<Socket<Protocol>>
                create(std::weak_ptr<BufferContainer<Protocol>> buffer_container,
                       std::chrono::milliseconds send_timeout = DefaultSendTimeout,
                       std::chrono::milliseconds receive_timeout = DefaultReceiveTimeout);
        
        static std::shared_ptr<Socket<Protocol>>
               create(std::shared_ptr<smooth::core::network::InetAddress> ip,
                      int socket_id,
                      std::weak_ptr<BufferContainer<Protocol>> buffer_container,
                      std::chrono::milliseconds send_timeout = DefaultSendTimeout,
                      std::chrono::milliseconds receive_timeout = DefaultReceiveTimeout);
                           
        ~UdpSocket() override = default;
       
        virtual void set_existing_socket(const std::shared_ptr<InetAddress>& address, int socket_id) override;
    protected:
        UdpSocket(std::weak_ptr<BufferContainer<Protocol>> buffer_container);

        virtual bool create_socket() override;
    };
    
    std::shared_ptr<Socket<Protocol>>
        UdpSocket::create(
            std::weak_ptr<BufferContainer<Protocol>> buffer_container,
            std::chrono::milliseconds send_timeout = DefaultSendTimeout,
            std::chrono::milliseconds receive_timeout = DefaultReceiveTimeout)
    {
        auto s = smooth::core::util::create_protected_shared<UdpSocket<Protocol, Packet>>(buffer_container);
        s->set_send_timeout(send_timeout);
        s->set_receive_timeout(receive_timeout);

        return s;
    }
    
    std::shared_ptr<Socket<Protocol>>
        UdpSocket::create(
            std::shared_ptr<smooth::core::network::InetAddress> ip,
            int socket_id,
            std::weak_ptr<BufferContainer<Protocol>> buffer_container,
            std::chrono::milliseconds send_timeout = DefaultSendTimeout,
            std::chrono::milliseconds receive_timeout = DefaultReceiveTimeout)
    {
        auto s = create(buffer_container, send_timeout, receive_timeout);
        s->set_send_timeout(send_timeout);
        s->set_receive_timeout(receive_timeout);
        s->set_existing_socket(ip, socket_id);

        return s;
    }
    
    UdpSocket::UdpSocket(std::weak_ptr<BufferContainer<Protocol>> buffer_container)
        : Socket(buffer_container)
    {
        // empty
    }
    
    void UdpSocket::set_existing_socket(const std::shared_ptr<InetAddress>& address, int socket_id)
    {
        this->ip = address;
        this->socket_id = socket_id;
        active = true;
        connected = true;
        set_non_blocking();

        SocketDispatcher::instance().perform_op(SocketOperation::Op::AddActiveSocket, shared_from_this());
    }
    
    virtual bool UdpSocket::create_socket()
    {
        bool res = false;

        if (socket_id < 0)
        {
            socket_id = socket(ip->get_protocol_family(), SOCK_DGRAM, 0);

            if (socket_id == INVALID_SOCKET)
            {
                loge("Failed to create socket");
            }
            else
            {
                res = set_non_blocking();
            }
        }
        else
        {
            res = true;
        }

        return res;
    }
}


#endif // ICOM__UDP_SOCKET_H