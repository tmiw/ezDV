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

#include "esp_event.h"
#include "esp_http_server.h"

#include "task/DVTask.h"

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
    virtual void onTaskWake_() override;
    virtual void onTaskSleep_() override;
    
private:
    httpd_handle_t configServerHandle_;
};

}

}

#endif // HTTP_SERVER_TASK_H