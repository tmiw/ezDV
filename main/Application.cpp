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

#include "Application.h"

#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include "esp_log.h"
#include "nvs_flash.h"

#define CURRENT_LOG_TAG ("App")

namespace ezdv
{
App::App()
    : ezdv::task::DVTask("MainApp", 1, 4096, tskNO_AFFINITY, 10, portMAX_DELAY)
    , wirelessTask_(nullptr)
{
    // empty
}

void App::onTaskStart_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "onTaskStart_");

    // Start Wi-Fi
    wirelessTask_ = new network::WirelessTask();
    assert(wirelessTask_ != nullptr);
    start(wirelessTask_, pdMS_TO_TICKS(5000));
}

void App::onTaskSleep_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "onTaskSleep_");

    // Sleep Wi-Fi
    if (wirelessTask_ != nullptr)
    {
        sleep(wirelessTask_, pdMS_TO_TICKS(5000));
    }
}

void App::onTaskTick_()
{
    // empty
}

}

ezdv::App* app;

// Global method to trigger sleep
void StartSleeping()
{
    app->sleep();
}

extern "C" void app_main()
{
    // Note: mandatory before using DVTask.
    DVTask::Initialize();

    // initialize flash
    ESP_ERROR_CHECK(nvs_flash_init());

    app = new ezdv::App();
    assert(app != nullptr);

    // Perform initial startup actions because we may not be fully ready yet
    app->start();
}
