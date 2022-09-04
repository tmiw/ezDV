#include "Application.h"

/*
#include "audio/TLV320.h"
#include "codec/FreeDVTask.h"
#include "radio/icom/IcomRadioTask.h"
#include "audio/AudioMixer.h"
#include "storage/SettingsManager.h"
*/

#include "driver/rtc_io.h"
#include "driver/gpio.h"
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
    , tlv320Device_(&i2cDevice_)
{
    // Link TLV320 output FIFOs to FreeDVTask
    tlv320Device_.setAudioOutput(
        audio::AudioInput::ChannelLabel::LEFT_CHANNEL, 
        freedvTask_.getAudioInput(audio::AudioInput::ChannelLabel::LEFT_CHANNEL)
    );

    tlv320Device_.setAudioOutput(
        audio::AudioInput::ChannelLabel::RIGHT_CHANNEL, 
        freedvTask_.getAudioInput(audio::AudioInput::ChannelLabel::RIGHT_CHANNEL)
    );

    // Link FreeDVTask output FIFOs to:
    //    * RX: AudioMixer left channel
    //    * TX: TLV320 right channel
    freedvTask_.setAudioOutput(
        audio::AudioInput::ChannelLabel::USER_CHANNEL, 
        audioMixer_.getAudioInput(audio::AudioInput::ChannelLabel::LEFT_CHANNEL)
    );

    freedvTask_.setAudioOutput(
        audio::AudioInput::ChannelLabel::RADIO_CHANNEL, 
        tlv320Device_.getAudioInput(audio::AudioInput::ChannelLabel::RADIO_CHANNEL)
    );

    // Link beeper output to AudioMixer right channel
    beeperTask_.setAudioOutput(
        audio::AudioInput::ChannelLabel::LEFT_CHANNEL,
        audioMixer_.getAudioInput(audio::AudioInput::ChannelLabel::RIGHT_CHANNEL)
    );

    // Link audio mixer to TLV320 left channel
    audioMixer_.setAudioOutput(
        audio::AudioInput::ChannelLabel::LEFT_CHANNEL,
        tlv320Device_.getAudioInput(audio::AudioInput::ChannelLabel::USER_CHANNEL)
    );
}

void App::onTaskStart_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "onTaskStart_");

    // Initialize LED array early as we want all the LEDs lit during the boot process.
    ledArray_.start();
    waitForStart(&ledArray_, pdMS_TO_TICKS(1000));

    {
        ezdv::driver::SetLedStateMessage msg(ezdv::driver::SetLedStateMessage::LedLabel::SYNC, true);
        ledArray_.post(&msg);
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::OVERLOAD;
        ledArray_.post(&msg);
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::PTT;
        ledArray_.post(&msg);
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::NETWORK;
        ledArray_.post(&msg);
    }

    // Start device drivers
    tlv320Device_.start();
    waitForStart(&tlv320Device_, pdMS_TO_TICKS(10000));

    buttonArray_.start();
    
    // Start audio processing
    freedvTask_.start();
    audioMixer_.start();
    beeperTask_.start();

    waitForStart(&freedvTask_, pdMS_TO_TICKS(1000));
    waitForStart(&audioMixer_, pdMS_TO_TICKS(1000));
    waitForStart(&beeperTask_, pdMS_TO_TICKS(1000));

    // Start UI
    uiTask_.start();
    waitForStart(&uiTask_, pdMS_TO_TICKS(1000));

    // Start storage handling
    settingsTask_.start();
}

void App::onTaskWake_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "onTaskWake_");
    
    // Initialize LED array early as we want all the LEDs lit during the boot process.
    ledArray_.wake();
    waitForAwake(&ledArray_, pdMS_TO_TICKS(1000));

    {
        ezdv::driver::SetLedStateMessage msg(ezdv::driver::SetLedStateMessage::LedLabel::SYNC, true);
        ledArray_.post(&msg);
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::OVERLOAD;
        ledArray_.post(&msg);
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::PTT;
        ledArray_.post(&msg);
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::NETWORK;
        ledArray_.post(&msg);
    }

    // Wake up device drivers
    tlv320Device_.wake();
    waitForAwake(&tlv320Device_, pdMS_TO_TICKS(10000));

    buttonArray_.wake();

    // Wake audio processing
    freedvTask_.wake();
    audioMixer_.wake();
    beeperTask_.wake();
    waitForAwake(&freedvTask_, pdMS_TO_TICKS(1000));
    waitForAwake(&audioMixer_, pdMS_TO_TICKS(1000));
    waitForAwake(&beeperTask_, pdMS_TO_TICKS(1000));

    // Wake UI
    uiTask_.wake();
    waitForAwake(&uiTask_, pdMS_TO_TICKS(1000));

    // Wake storage handling
    settingsTask_.wake();
}

void App::onTaskSleep_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "onTaskSleep_");

    // Sleep UI
    uiTask_.sleep();
    waitForSleep(&uiTask_, pdMS_TO_TICKS(1000));

    // Sleep storage handling
    settingsTask_.sleep();
    waitForSleep(&settingsTask_, pdMS_TO_TICKS(1000));

    // Delay a second or two to allow final beeper to play
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Sleep audio processing
    beeperTask_.sleep();
    waitForSleep(&beeperTask_, pdMS_TO_TICKS(1000));

    freedvTask_.sleep();
    waitForSleep(&freedvTask_, pdMS_TO_TICKS(1000));

    audioMixer_.sleep();
    waitForSleep(&audioMixer_, pdMS_TO_TICKS(1000));

    // Sleep device drivers
    tlv320Device_.sleep();
    waitForSleep(&tlv320Device_, pdMS_TO_TICKS(1000));

    buttonArray_.sleep();
    ledArray_.sleep();
    waitForSleep(&buttonArray_, pdMS_TO_TICKS(1000));
    waitForSleep(&ledArray_, pdMS_TO_TICKS(1000));
    
    /* Initialize mode button GPIO as RTC IO, enable input, disable pullup and pulldown */
    rtc_gpio_init(GPIO_NUM_5);
    rtc_gpio_set_direction(GPIO_NUM_5, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(GPIO_NUM_5);
    rtc_gpio_pullup_dis(GPIO_NUM_5);
    rtc_gpio_hold_en(GPIO_NUM_5);
    
    esp_err_t err = ulp_riscv_load_binary(ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start));
    ESP_ERROR_CHECK(err);

    /* Start the ULV program */
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

// Global method to trigger sleep
void StartSleeping()
{
    app->sleep();
}

extern "C" void app_main()
{
    // Make sure the ULP program isn't running.
    ulp_riscv_timer_stop();
    ulp_riscv_halt();

    // Note: mandatory before using DVTask.
    DVTask::Initialize();

    // Note: GPIO ISRs use per GPIO ISRs.
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    
    app = new ezdv::App();
    assert(app != nullptr);

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    /* not a wakeup from ULP, load the firmware */
    if (cause != ESP_SLEEP_WAKEUP_ULP) {
        // Perform initial startup actions because we may not be fully ready yet
        app->start();

        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(CURRENT_LOG_TAG, "Starting power off application");
        app->sleep();
    }
    else
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Woken up via ULP, booting...");
        app->wake();
    }
}
