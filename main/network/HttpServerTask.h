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

#ifndef HTTP_SERVER_TASK_H
#define HTTP_SERVER_TASK_H

#include <vector>

#include "esp_event.h"
#include "esp_http_server.h"
#include "cJSON.h"

#include "task/DVTask.h"
#include "network/NetworkMessage.h"

extern "C"
{
    DV_EVENT_DECLARE_BASE(HTTP_SERVER_MESSAGE);
}

namespace ezdv
{

namespace network
{

using namespace ezdv::task;

class HttpServerTask : public DVTask
{
public:
    HttpServerTask();
    virtual ~HttpServerTask();
        
protected:
    virtual void onTaskStart_() override;
    virtual void onTaskSleep_() override;
    
private:
    enum HttpRequestId 
    {
        WEBSOCKET_CONNECTED = 1,
        WEBSOCKET_DISCONNECTED = 2,
        SERVE_STATIC_FILE = 14,
    };
    
    template<uint32_t MSG_ID>
    class HttpEventMessageCommon : public DVTaskMessageBase<MSG_ID, HttpEventMessageCommon<MSG_ID>>
    {
    public:
        HttpEventMessageCommon(int fdProvided = 0)
            : DVTaskMessageBase<MSG_ID, HttpEventMessageCommon<MSG_ID>>(HTTP_SERVER_MESSAGE)
            , fd(fdProvided)
            {}
        virtual ~HttpEventMessageCommon() = default;

        int fd;
    };
    
    using HttpWebsocketConnectedMessage = HttpEventMessageCommon<WEBSOCKET_CONNECTED>;
    using HttpWebsocketDisconnectedMessage = HttpEventMessageCommon<WEBSOCKET_DISCONNECTED>;
    
    template<uint32_t MSG_ID>
    class HttpRequestMessageCommon : public DVTaskMessageBase<MSG_ID, HttpRequestMessageCommon<MSG_ID>>
    {
    public:
        HttpRequestMessageCommon(int fdProvided = 0, cJSON* requestProvided = nullptr)
            : DVTaskMessageBase<MSG_ID, HttpRequestMessageCommon<MSG_ID>>(HTTP_SERVER_MESSAGE)
            , fd(fdProvided)
            , request(requestProvided)
            {}
        virtual ~HttpRequestMessageCommon() = default;

        int fd;
        cJSON* request;
    };

    class HttpServeStaticFileMessage : public DVTaskMessageBase<SERVE_STATIC_FILE, HttpServeStaticFileMessage>
    {
    public:
        HttpServeStaticFileMessage(int fdProvided = 0, httpd_req_t* reqProvided = nullptr)
            : DVTaskMessageBase<SERVE_STATIC_FILE, HttpServeStaticFileMessage>(HTTP_SERVER_MESSAGE)
            , fd(fdProvided)
            , request(reqProvided)
            {}
        virtual ~HttpServeStaticFileMessage() = default;

        int fd;
        httpd_req_t* request;
    };
    
    using WebSocketList = std::map<int, bool>; // int = socket ID, bool = currently scanning Wi-Fi networks
    
    httpd_handle_t configServerHandle_;
    WebSocketList activeWebSockets_;
    bool isRunning_;
    
    void onHttpWebsocketConnectedMessage_(DVTask* origin, HttpWebsocketConnectedMessage* message);
    void onHttpWebsocketDisconnectedMessage_(DVTask* origin, HttpWebsocketDisconnectedMessage* message);
    
    // Helper to asynchronously serve static files.
    void onHttpServeStaticFileMessage_(DVTask* origin, HttpServeStaticFileMessage* message);
    
    void sendJSONMessage_(cJSON* message, WebSocketList& socketList);
    
    static esp_err_t ServeStaticPage_(httpd_req_t *req);
    static esp_err_t ServeWebsocketPage_(httpd_req_t *req);
};

}

}

#endif // HTTP_SERVER_TASK_H
