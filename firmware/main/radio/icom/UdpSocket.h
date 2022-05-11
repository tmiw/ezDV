#ifndef ICOM__UDP_SOCKET_H
#define ICOM__UDP_SOCKET_H

#include <memory>
#include "smooth/core/network/CommonSocket.h"
#include "smooth/core/network/Socket.h"
#include "smooth/core/network/SocketDispatcher.h"

namespace sm1000neo::radio::icom
{
    // This is an override of Smooth's Socket class that creates the socket as UDP.
    // Smooth by default only creates TCP sockets. Based on the Smooth source code,
    // it should be okay just to override a few methods to ensure we create sockets
    // as SOCK_DGRAM and not set TCP_NODELAY.
    template<typename Protocol, typename Packet = typename Protocol::packet_type>
    class UdpSocket 
        : public smooth::core::network::Socket<Protocol, Packet>
    {
    public:
        static std::shared_ptr<smooth::core::network::Socket<Protocol>>
                create(std::weak_ptr<smooth::core::network::BufferContainer<Protocol>> buffer_container,
                       std::chrono::milliseconds send_timeout = smooth::core::network::DefaultSendTimeout,
                       std::chrono::milliseconds receive_timeout = smooth::core::network::DefaultReceiveTimeout)
        {
            auto s = smooth::core::util::create_protected_shared<UdpSocket<Protocol, Packet>>(buffer_container);
            s->set_send_timeout(send_timeout);
            s->set_receive_timeout(receive_timeout);

            return s;
        }

        static std::shared_ptr<smooth::core::network::Socket<Protocol>>
               create(std::shared_ptr<smooth::core::network::InetAddress> ip,
                      int socket_id,
                      std::weak_ptr<smooth::core::network::BufferContainer<Protocol>> buffer_container,
                      std::chrono::milliseconds send_timeout = smooth::core::network::DefaultSendTimeout,
                      std::chrono::milliseconds receive_timeout = smooth::core::network::DefaultReceiveTimeout)
        {
            auto s = create(buffer_container, send_timeout, receive_timeout);
            s->set_send_timeout(send_timeout);
            s->set_receive_timeout(receive_timeout);
            s->set_existing_socket(ip, socket_id);

            return s;
        }
                           
        ~UdpSocket() override = default;
       
        virtual void set_existing_socket(const std::shared_ptr<smooth::core::network::InetAddress>& address, int socket_id) override
        {
            this->ip = address;
            this->socket_id = socket_id;
            this->set_non_blocking();

            smooth::core::network::SocketDispatcher::instance().perform_op(
                smooth::core::network::SocketOperation::Op::AddActiveSocket, 
                this->shared_from_this());
        }
    protected:
        UdpSocket(std::weak_ptr<smooth::core::network::BufferContainer<Protocol>> buffer_container)
            : smooth::core::network::Socket<Protocol, Packet>(buffer_container)
        {
            // empty
        }

        virtual bool create_socket() override
        {
            bool res = false;

            if (this->socket_id < 0)
            {
                this->socket_id = socket(this->ip->get_protocol_family(), SOCK_DGRAM, IPPROTO_UDP);

                if (this->socket_id == smooth::core::network::ISocket::INVALID_SOCKET)
                {
                    this->loge("Failed to create socket");
                }
                else
                {
                    res = this->set_non_blocking();
                }
            }
            else
            {
                res = true;
            }

            return res;
        }
    };
}


#endif // ICOM__UDP_SOCKET_H