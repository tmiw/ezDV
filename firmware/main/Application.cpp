#include "Application.h"
#include "smooth/core/task_priorities.h"
#include "smooth/core/SystemStatistics.h"

#include "audio/TLV320.h"
#include "codec/FreeDVTask.h"
#include "radio/icom/IcomRadioTask.h"
#include "audio/AudioMixer.h"
#include "storage/SettingsManager.h"

#include "driver/rtc_io.h"
#include "esp32s3/ulp.h"
#include "esp32s3/ulp_riscv.h"
#include "ulp_main.h"
#include "esp_sleep.h"

using namespace smooth;
using namespace smooth::core;

#define CURRENT_LOG_TAG ("app")

namespace ezdv
{
    App::App()
        : Application(APPLICATION_BASE_PRIO, std::chrono::milliseconds(1000))
    {
        // empty
    }

    void App::init()
    {
        Application::init();
        
        // Ensure settings are live before proceeding.
        ezdv::storage::SettingsManager::ThisTask().start();
        
        ezdv::audio::TLV320::ThisTask().start();
        ezdv::codec::FreeDVTask::ThisTask().start();
        ezdv::audio::AudioMixer::ThisTask().start();
        uiTask.start();

#if 0    
        smooth::core::network::Wifi& wifi = get_wifi();
        wifi.set_host_name("ezdv");
        wifi.set_auto_connect(true);
        wifi.set_ap_credentials("YOUR WIFI NETWORK", "YOUR WIFI PASSWORD");
        wifi.connect_to_ap();
        
        vTaskDelay(pdMS_TO_TICKS(10000));
        auto& radioTask = ezdv::radio::icom::IcomRadioTask::ThisTask();
        radioTask.setLocalIp(wifi.get_local_ip());
        radioTask.setAuthInfo("192.168.59.1", 50001, "YOUR RADIO USERNAME", "YOUR RADIO PASSWORD");
        radioTask.start();
#endif
    }
}

extern "C"
{
    
// Power off handler application
extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");
    
#ifdef ESP_PLATFORM
void load_ulp_and_shutdown(void)
{
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

    ESP_ERROR_CHECK( esp_sleep_enable_ulp_wakeup());
    esp_deep_sleep_start();    
}

void app_main()
{
    // Make sure the ULP program isn't running.
    //ulp_riscv_timer_stop();
    //ulp_riscv_halt();
    
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    /* not a wakeup from ULP, load the firmware */
    if (cause != ESP_SLEEP_WAKEUP_ULP) {
        ESP_LOGI(CURRENT_LOG_TAG, "Starting power off application");
        load_ulp_and_shutdown();
    }
    else
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Woken up via ULP, booting...");
    }
        
    ezdv::App app{};
    app.start();
}
#else
int main(int /*argc*/, char** /*argv*/)
{
    smooth::core::SystemStatistics::instance().dump();
    ezdv::App app{};
    app.start();
    return 0;
}
#endif

}
