#include "Application.h"

/*
#include "audio/TLV320.h"
#include "codec/FreeDVTask.h"
#include "radio/icom/IcomRadioTask.h"
#include "audio/AudioMixer.h"
#include "storage/SettingsManager.h"
*/

#include "driver/rtc_io.h"
#include "ulp_riscv.h"
#include "ulp_main.h"
#include "esp_sleep.h"
#include "esp_log.h"

#define CURRENT_LOG_TAG ("app")

extern "C"
{
    // Power off handler application
    extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
    extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");
}

namespace ezdv
{
App::App()
    : ezdv::task::DVTask("MainApp", 1, 4096, tskNO_AFFINITY, 10)
{
    // empty
}

void App::onTaskStart_(DVTask* origin, TaskStartMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "onTaskStart_");
}

void App::onTaskWake_(DVTask* origin, TaskWakeMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "onTaskWake_");
}

void App::onTaskSleep_(DVTask* origin, TaskSleepMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "onTaskSleep_");

    // TBD - sleep other tasks.

    /* Initialize mode button GPIO as RTC IO, enable input, disable pullup and pulldown */
    rtc_gpio_init(GPIO_NUM_5);
    rtc_gpio_set_direction(GPIO_NUM_5, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(GPIO_NUM_5);
    rtc_gpio_pullup_dis(GPIO_NUM_5);
    rtc_gpio_hold_en(GPIO_NUM_5);
    
    esp_err_t err = ulp_riscv_load_binary(ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start));
    ESP_ERROR_CHECK(err);

    /* The first argument is the period index, which is not used by the ULP-RISC-V timer
     * The second argument is the period in microseconds, which gives a wakeup time period of: 20ms
     */
    //ulp_set_wakeup_period(0, 20000);

    /* Start the program */
    err = ulp_riscv_run();
    ESP_ERROR_CHECK(err);
    
    // Halt application
    ESP_LOGI(CURRENT_LOG_TAG, "Halting system");
    
    /* Small delay to ensure the messages are printed */
    vTaskDelay(100);

    ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());
    esp_deep_sleep_start();    
}

}

ezdv::App* app;

extern "C" void app_main()
{
    // Make sure the ULP program isn't running.
    ulp_riscv_timer_stop();
    ulp_riscv_halt();

    // Note: mandatory for publish to work.
    esp_event_loop_create_default();
    
    app = new ezdv::App();
    assert(app != nullptr);

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    /* not a wakeup from ULP, load the firmware */
    if (cause != ESP_SLEEP_WAKEUP_ULP) {
        // Perform initial startup actions because we may not be fully ready yet
        app->start();

        ESP_LOGI(CURRENT_LOG_TAG, "Starting power off application");
        app->sleep();
    }
    else
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Woken up via ULP, booting...");
        app->wake();
    }
}
