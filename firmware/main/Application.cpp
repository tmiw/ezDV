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

//======================================================================
// DEBUGGING OPTIONS (TBD: add as ESP-IDF menuconfig options)
//======================================================================
#define TASK_TICK_DEBUGGING /* Enables debugging output every ~1 second. */
#define PRINT_PROCESS_STATS /* Prints process stats (e.g. CPU usage). */

//======================================================================
// No user-configurable options beyond this point!
//======================================================================

#include "Application.h"

#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "ulp_riscv.h"
#include "ulp_main.h"
#include "esp_sleep.h"
#include "esp_log.h"

#if defined(ENABLE_AUTOMATED_TX_RX_TEST)
#include "driver/ButtonMessage.h"
#include "audio/FreeDVMessage.h"
#endif // ENABLE_AUTOMATED_TX_RX_TEST

#define CURRENT_LOG_TAG ("app")

#define BOOTUP_VOL_DOWN_GPIO (GPIO_NUM_7)
#define BOOTUP_PTT_GPIO (GPIO_NUM_4)
#define TLV320_RESET_GPIO (GPIO_NUM_13)

#if defined(PRINT_PROCESS_STATS)
#define ARRAY_SIZE_OFFSET   5   //Increase this if print_real_time_stats returns ESP_ERR_INVALID_SIZE
#endif // defined(PRINT_PROCESS_STATS)

extern "C"
{
    // Power off handler application
    extern const uint8_t ulp_main_bin_start[] asm("_binary_ulp_main_bin_start");
    extern const uint8_t ulp_main_bin_end[]   asm("_binary_ulp_main_bin_end");
    
    bool rebootDevice = false;
}

namespace ezdv
{
App::App()
    : ezdv::task::DVTask("MainApp", 1, 4096, tskNO_AFFINITY, 10, pdMS_TO_TICKS(1000))
    , audioMixer_(nullptr)
    , beeperTask_(nullptr)
    , freedvTask_(nullptr)
    , max17048_(&i2cDevice_)
    , tlv320Device_(nullptr)
    , wirelessTask_(nullptr)
    , settingsTask_(nullptr)
    , softwareUpdateTask_(nullptr)
    , uiTask_(nullptr)
    , rfComplianceTask_(nullptr)
    , fuelGaugeTask_(nullptr)
    , voiceKeyerTask_(nullptr)
    , rfComplianceEnabled_(false)
    , wifiOverrideEnabled_(false)
{
    // empty
}

void App::checkOverrides_()
{
    // Check to see if Vol Down is being held on startup. 
    // If so, force use of default Wi-Fi setup. Note that 
    // we have to duplicate the initial pin setup here 
    // since waiting until the UI is fully up may be too
    // late for Wi-Fi.
    ESP_ERROR_CHECK(gpio_reset_pin(BOOTUP_VOL_DOWN_GPIO));
    ESP_ERROR_CHECK(gpio_set_direction(BOOTUP_VOL_DOWN_GPIO, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(BOOTUP_VOL_DOWN_GPIO, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(gpio_pullup_en(BOOTUP_VOL_DOWN_GPIO));

    if (gpio_get_level(BOOTUP_VOL_DOWN_GPIO) == 0)
    {
        wifiOverrideEnabled_ = true;
    }
    
    // Check to see if PTT is beind held on startup.
    // This triggers the RF compliance test system.
    ESP_ERROR_CHECK(gpio_reset_pin(BOOTUP_PTT_GPIO));
    ESP_ERROR_CHECK(gpio_set_direction(BOOTUP_PTT_GPIO, GPIO_MODE_INPUT));
    ESP_ERROR_CHECK(gpio_set_pull_mode(BOOTUP_PTT_GPIO, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(gpio_pullup_en(BOOTUP_PTT_GPIO));
    
    if (gpio_get_level(BOOTUP_PTT_GPIO) == 0)
    {
        rfComplianceEnabled_ = true;
    }
}

void App::enablePeripheralPower_()
{
    // Reset GPIO0 to prevent glitches
    rtc_gpio_init(GPIO_NUM_0);
    rtc_gpio_set_direction(GPIO_NUM_0, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_set_direction_in_sleep(GPIO_NUM_0, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(GPIO_NUM_0);
    rtc_gpio_pullup_dis(GPIO_NUM_0);
    rtc_gpio_hold_en(GPIO_NUM_0);
    
    // TLV320 related GPIOs need to be isolated prior to enabling
    // peripheral power. If we don't do this, the following happens
    // the first time after waking up from deep sleep:
    //
    // 1. Network (and potentially other LEDs) stop working, and
    // 2. Audio glitches occur on startup.
    std::vector<gpio_num_t> tlv320Gpios { 
        GPIO_NUM_3, GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11,
        GPIO_NUM_12, GPIO_NUM_14, TLV320_RESET_GPIO };
    for (auto& gpio : tlv320Gpios)
    {
        rtc_gpio_init(gpio);
        rtc_gpio_set_direction(gpio, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_set_direction_in_sleep(gpio, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pulldown_en(gpio);
        rtc_gpio_pullup_dis(gpio);
        rtc_gpio_hold_en(gpio);
    }
    
    // Sleep for above changes to take effect.
    vTaskDelay(pdMS_TO_TICKS(10));

    // Enable peripheral power (required for v0.4+). This will automatically
    // power down once we switch to the ULP processor on shutdown, reducing
    // "off" current considerably.
    rtc_gpio_init(GPIO_NUM_17);
    rtc_gpio_hold_dis(GPIO_NUM_17);
    rtc_gpio_set_direction(GPIO_NUM_17, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(GPIO_NUM_17, true);
    rtc_gpio_hold_en(GPIO_NUM_17);
    
    // Sleep until peripheral power activates.
    vTaskDelay(pdMS_TO_TICKS(10));

    // Now we can re-attach TLV320 related GPIOs and get
    // ready to configure it.
    for (auto& gpio : tlv320Gpios)
    {
        rtc_gpio_hold_dis(gpio);
        rtc_gpio_deinit(gpio);
        gpio_reset_pin(gpio);
    }
    
    rtc_gpio_hold_dis(GPIO_NUM_0);
    rtc_gpio_deinit(GPIO_NUM_0);
    gpio_reset_pin(GPIO_NUM_0);
    
    // Sleep for GPIO reattach to take effect.
    vTaskDelay(pdMS_TO_TICKS(10));
}

void App::enterDeepSleep_()
{
    ulp_power_up_mode = 0;

    /* Initialize mode button GPIO as RTC IO, enable input, enable pullup */
    rtc_gpio_init(GPIO_NUM_5);
    rtc_gpio_set_direction(GPIO_NUM_5, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(GPIO_NUM_5);
    rtc_gpio_pullup_en(GPIO_NUM_5);
    rtc_gpio_hold_en(GPIO_NUM_5);
    
    /* Initialize battery detect GPIO (GPIO0) as RTC IO, enable input, enable pulldown */
    rtc_gpio_init(GPIO_NUM_0);
    rtc_gpio_set_direction(GPIO_NUM_0, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(GPIO_NUM_0);
    rtc_gpio_pullup_dis(GPIO_NUM_0);
    rtc_gpio_hold_en(GPIO_NUM_0);
    
    // Isolate TLV320 related GPIOs to prevent issues when coming back from sleep
    // (see app_start() for explanation).
    std::vector<gpio_num_t> tlv320Gpios { 
        GPIO_NUM_3, GPIO_NUM_9, GPIO_NUM_10, GPIO_NUM_11,
        GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14, 
        TLV320_RESET_GPIO };
    for (auto& gpio : tlv320Gpios)
    {
        rtc_gpio_init(gpio);
        rtc_gpio_set_direction(gpio, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_set_direction_in_sleep(gpio, RTC_GPIO_MODE_INPUT_ONLY);
        rtc_gpio_pulldown_en(gpio);
        rtc_gpio_pullup_dis(gpio);
        rtc_gpio_hold_en(gpio);
    }
    
    // Sleep for GPIO changes to take effect.
    vTaskDelay(pdMS_TO_TICKS(10));

    /* Shut off peripheral power. */
    rtc_gpio_init(GPIO_NUM_17);
    rtc_gpio_hold_dis(GPIO_NUM_17);
    rtc_gpio_set_direction(GPIO_NUM_17, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_direction_in_sleep(GPIO_NUM_17, RTC_GPIO_MODE_OUTPUT_ONLY);
    rtc_gpio_set_level(GPIO_NUM_17, false);
    rtc_gpio_hold_en(GPIO_NUM_17);
    
    // Sleep for power-down to take effect.
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t err = ulp_riscv_load_binary(ulp_main_bin_start, (ulp_main_bin_end - ulp_main_bin_start));
    ESP_ERROR_CHECK(err);
    
    // Halt application
    ESP_LOGI(CURRENT_LOG_TAG, "Halting system");
    
    /* Small delay to ensure the messages are printed */
    vTaskDelay(100);
    fflush(stdout);
    vTaskDelay(100);

    if (rebootDevice)
    {
        esp_restart();
    }
    else
    {
        /* Start the ULV program */
        ESP_ERROR_CHECK(ulp_set_wakeup_period(0, 100 * 1000)); // 100 ms * (1000 us/ms)
        err = ulp_riscv_run();
        ESP_ERROR_CHECK(err);
        
        ESP_ERROR_CHECK(esp_sleep_enable_ulp_wakeup());
        esp_deep_sleep_start();    
    }
}

void App::onTaskStart_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "onTaskStart_");

    // Enable peripheral power.
    enablePeripheralPower_();
    
    // Check for overrides and perform initial audio routing.
    checkOverrides_();

    // The battery driver should also be initialized early in case we
    // need to immediately sleep due to low power.
    max17048_.suppressForcedSleep(ulp_power_up_mode == 2);
    start(&max17048_, pdMS_TO_TICKS(2000));

    if (max17048_.isLowSOC() || ulp_power_up_mode == 1)
    {
        ulp_power_up_mode = 0;
        enterDeepSleep_();
    }

    // Initialize LED array early as we want all the LEDs lit during the boot process.
    start(&ledArray_, pdMS_TO_TICKS(1000));

    if (ulp_power_up_mode != 2)
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
    else
    {
        // Turn on LEDs from bottom to top and then turn off from top to bottom
        // to indicate that we're booting into battery charge mode.
        ezdv::driver::SetLedStateMessage msg(ezdv::driver::SetLedStateMessage::LedLabel::NETWORK, true);
        ledArray_.post(&msg);
        
        vTaskDelay(pdMS_TO_TICKS(250));
        
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::OVERLOAD;
        ledArray_.post(&msg);
        
        vTaskDelay(pdMS_TO_TICKS(250));
        
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::SYNC;
        ledArray_.post(&msg);
        
        vTaskDelay(pdMS_TO_TICKS(250));
        
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::PTT;
        ledArray_.post(&msg);
        
        vTaskDelay(pdMS_TO_TICKS(250));
        
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::PTT;
        msg.ledState = false;
        ledArray_.post(&msg);
        
        vTaskDelay(pdMS_TO_TICKS(250));
        
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::SYNC;
        ledArray_.post(&msg);
        
        vTaskDelay(pdMS_TO_TICKS(250));
        
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::OVERLOAD;
        ledArray_.post(&msg);
        
        vTaskDelay(pdMS_TO_TICKS(250));
        
        msg.led = ezdv::driver::SetLedStateMessage::LedLabel::NETWORK;
        ledArray_.post(&msg);
        
        vTaskDelay(pdMS_TO_TICKS(250));
    }

    // Start device drivers
    start(&buttonArray_, pdMS_TO_TICKS(1000));

    if (ulp_power_up_mode == 2)
    {
        fuelGaugeTask_ = new ui::FuelGaugeTask();
        assert(fuelGaugeTask_ != nullptr);
        start(fuelGaugeTask_, pdMS_TO_TICKS(1000));
    }
    else
    {
        tlv320Device_ = new driver::TLV320(&i2cDevice_);
        assert(tlv320Device_ != nullptr);
        
        start(tlv320Device_, pdMS_TO_TICKS(10000));
    
        if (!rfComplianceEnabled_)
        {
            freedvTask_ = new audio::FreeDVTask();
            assert(freedvTask_ != nullptr);
            
            audioMixer_ = new audio::AudioMixer();
            assert(audioMixer_ != nullptr);
            
            beeperTask_ = new audio::BeeperTask();
            assert(beeperTask_ != nullptr);
            
            // Link TLV320 output FIFOs to FreeDVTask
            tlv320Device_->setAudioOutput(
                audio::AudioInput::ChannelLabel::LEFT_CHANNEL, 
                freedvTask_->getAudioInput(audio::AudioInput::ChannelLabel::LEFT_CHANNEL)
            );

            tlv320Device_->setAudioOutput(
                audio::AudioInput::ChannelLabel::RIGHT_CHANNEL, 
                freedvTask_->getAudioInput(audio::AudioInput::ChannelLabel::RIGHT_CHANNEL)
            );

            // Link FreeDVTask output FIFOs to:
            //    * RX: AudioMixer left channel
            //    * TX: TLV320 right channel
            freedvTask_->setAudioOutput(
                audio::AudioInput::ChannelLabel::USER_CHANNEL, 
                audioMixer_->getAudioInput(audio::AudioInput::ChannelLabel::LEFT_CHANNEL)
            );

            freedvTask_->setAudioOutput(
                audio::AudioInput::ChannelLabel::RADIO_CHANNEL, 
                tlv320Device_->getAudioInput(audio::AudioInput::ChannelLabel::RADIO_CHANNEL)
            );

            // Link beeper output to AudioMixer right channel
            beeperTask_->setAudioOutput(
                audio::AudioInput::ChannelLabel::LEFT_CHANNEL,
                audioMixer_->getAudioInput(audio::AudioInput::ChannelLabel::RIGHT_CHANNEL)
            );

            // Link audio mixer to TLV320 left channel
            audioMixer_->setAudioOutput(
                audio::AudioInput::ChannelLabel::LEFT_CHANNEL,
                tlv320Device_->getAudioInput(audio::AudioInput::ChannelLabel::USER_CHANNEL)
            );
                
            // Start audio processing
            start(freedvTask_, pdMS_TO_TICKS(1000));
            start(audioMixer_, pdMS_TO_TICKS(1000));
            start(beeperTask_, pdMS_TO_TICKS(1000));

            // Start UI
            voiceKeyerTask_ = new audio::VoiceKeyerTask(tlv320Device_, freedvTask_);
            assert(voiceKeyerTask_ != nullptr);
            start(voiceKeyerTask_, pdMS_TO_TICKS(1000));
            
            uiTask_ = new ui::UserInterfaceTask();
            assert(uiTask_ != nullptr);
            start(uiTask_, pdMS_TO_TICKS(1000));
        
            // Start Wi-Fi
            wirelessTask_ = new network::WirelessTask(freedvTask_, tlv320Device_, audioMixer_, voiceKeyerTask_);
            assert(wirelessTask_ != nullptr);
            
            wirelessTask_->setWiFiOverride(wifiOverrideEnabled_);
            start(wirelessTask_, pdMS_TO_TICKS(5000));

            // Start storage handling
            settingsTask_ = new storage::SettingsTask();
            assert(settingsTask_ != nullptr);
            settingsTask_->start();
            
            softwareUpdateTask_ = new storage::SoftwareUpdateTask();
            assert(softwareUpdateTask_ != nullptr);
            softwareUpdateTask_->start();
            
            // Mark boot as successful, no need to rollback.
            esp_ota_mark_app_valid_cancel_rollback();
        }
        else
        {
            rfComplianceTask_ = new ui::RfComplianceTestTask(&ledArray_, tlv320Device_);
            assert(rfComplianceTask_ != nullptr);
            
            // RF compliance task should be piped to TLV320.
            rfComplianceTask_->setAudioOutput(
                audio::AudioInput::ChannelLabel::LEFT_CHANNEL,
                tlv320Device_->getAudioInput(audio::AudioInput::ChannelLabel::USER_CHANNEL)
            );
            
            rfComplianceTask_->setAudioOutput(
                audio::AudioInput::ChannelLabel::RIGHT_CHANNEL,
                tlv320Device_->getAudioInput(audio::AudioInput::ChannelLabel::RADIO_CHANNEL)
            );
            
            start(rfComplianceTask_, pdMS_TO_TICKS(1000));
        }
    }
}

void App::onTaskSleep_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "onTaskSleep_");

    // Disable buttons
    sleep(&buttonArray_, pdMS_TO_TICKS(1000));

    if (ulp_power_up_mode == 2)
    {
        if (fuelGaugeTask_ != nullptr)
        {
            sleep(fuelGaugeTask_, pdMS_TO_TICKS(2000));
        }
    }
    else
    {
        if (!rfComplianceEnabled_)
        {
            // Sleep Wi-Fi
            if (wirelessTask_ != nullptr)
            {
                sleep(wirelessTask_, pdMS_TO_TICKS(5000));
            }
            
            // Sleep UI
            if (uiTask_ != nullptr)
            {
                sleep(uiTask_, pdMS_TO_TICKS(1000));
            }
            
            if (voiceKeyerTask_ != nullptr)
            {
                sleep(voiceKeyerTask_, pdMS_TO_TICKS(1000));
            }
            
            // Sleep storage handling
            if (settingsTask_ != nullptr)
            {
                sleep(settingsTask_, pdMS_TO_TICKS(1000));
            }
            
            // Sleep SW update
            if (softwareUpdateTask_ != nullptr)
            {
                sleep(softwareUpdateTask_, pdMS_TO_TICKS(1000));
            }
            
            // Delay a second or two to allow final beeper to play.
            if (beeperTask_ != nullptr)
            {
                sleep(beeperTask_, pdMS_TO_TICKS(7000));
            }
            
            // Sleep audio processing
            if (freedvTask_ != nullptr)
            {
                sleep(freedvTask_, pdMS_TO_TICKS(1000));
            }
            
            if (audioMixer_ != nullptr)
            {
                sleep(audioMixer_, pdMS_TO_TICKS(3000));
            }
        }
        else
        {
            if (rfComplianceTask_ != nullptr)
            {
                sleep(rfComplianceTask_, pdMS_TO_TICKS(1000));
            }
        }
        
        if (tlv320Device_ != nullptr)
        {
            sleep(tlv320Device_, pdMS_TO_TICKS(2000));
        }
    }
    
    // Sleep device drivers
    sleep(&ledArray_, pdMS_TO_TICKS(1000));
    sleep(&max17048_, pdMS_TO_TICKS(1000));
    
    enterDeepSleep_();
}

#if defined(PRINT_PROCESS_STATS)
/**
 * @brief   Function to print the CPU usage of tasks over a given duration.
 *
 * This function will measure and print the CPU usage of tasks over a specified
 * number of ticks (i.e. real time stats). This is implemented by simply calling
 * uxTaskGetSystemState() twice separated by a delay, then calculating the
 * differences of task run times before and after the delay.
 *
 * @note    If any tasks are added or removed during the delay, the stats of
 *          those tasks will not be printed.
 * @note    This function should be called from a high priority task to minimize
 *          inaccuracies with delays.
 * @note    When running in dual core mode, each core will correspond to 50% of
 *          the run time.
 *
 * @param   xTicksToWait    Period of stats measurement
 *
 * @return
 *  - ESP_OK                Success
 *  - ESP_ERR_NO_MEM        Insufficient memory to allocated internal arrays
 *  - ESP_ERR_INVALID_SIZE  Insufficient array size for uxTaskGetSystemState. Trying increasing ARRAY_SIZE_OFFSET
 *  - ESP_ERR_INVALID_STATE Delay duration too short
 */
static esp_err_t print_real_time_stats(TickType_t xTicksToWait)
{
    TaskStatus_t *start_array = NULL, *end_array = NULL;
    UBaseType_t start_array_size, end_array_size;
    configRUN_TIME_COUNTER_TYPE start_run_time, end_run_time;
    esp_err_t ret;

    //Allocate array to store current task states
    uint32_t total_elapsed_time = 0;
    start_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    start_array = (TaskStatus_t*)malloc(sizeof(TaskStatus_t) * start_array_size);
    if (start_array == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto exit_fn;
    }
    //Get current task states
    start_array_size = uxTaskGetSystemState(start_array, start_array_size, &start_run_time);
    if (start_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        goto exit_fn;
    }

    vTaskDelay(xTicksToWait);

    //Allocate array to store tasks states post delay
    end_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    end_array = (TaskStatus_t*)malloc(sizeof(TaskStatus_t) * end_array_size);
    if (end_array == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto exit_fn;
    }
    //Get post delay task states
    end_array_size = uxTaskGetSystemState(end_array, end_array_size, &end_run_time);
    if (end_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        goto exit_fn;
    }

    //Calculate total_elapsed_time in units of run time stats clock period.
    total_elapsed_time = (end_run_time - start_run_time);
    if (total_elapsed_time == 0) {
        ret = ESP_ERR_INVALID_STATE;
        goto exit_fn;
    }

    printf("| Task | Run Time | Percentage\n");
    //Match each task in start_array to those in the end_array
    for (int i = 0; i < start_array_size; i++) {
        int k = -1;
        for (int j = 0; j < end_array_size; j++) {
            if (start_array[i].xHandle == end_array[j].xHandle) {
                k = j;
                //Mark that task have been matched by overwriting their handles
                start_array[i].xHandle = NULL;
                end_array[j].xHandle = NULL;
                break;
            }
        }
        //Check if matching task found
        if (k >= 0) {
            uint32_t task_elapsed_time = end_array[k].ulRunTimeCounter - start_array[i].ulRunTimeCounter;
            uint32_t percentage_time = (task_elapsed_time * 100UL) / (total_elapsed_time * 2);
            printf("| %s | %" PRIu32 " | %" PRIu32 "%%\n", start_array[i].pcTaskName, task_elapsed_time, percentage_time);
        }
    }

    //Print unmatched tasks
    for (int i = 0; i < start_array_size; i++) {
        if (start_array[i].xHandle != NULL) {
            printf("| %s | Deleted\n", start_array[i].pcTaskName);
        }
    }
    for (int i = 0; i < end_array_size; i++) {
        if (end_array[i].xHandle != NULL) {
            printf("| %s | Created\n", end_array[i].pcTaskName);
        }
    }
    ret = ESP_OK;

exit_fn:    //Common return path
    free(start_array);
    free(end_array);
    return ret;
}
#endif // define(PRINT_PROCESS_STATS)

void App::onTaskTick_()
{
#if defined(TASK_TICK_DEBUGGING)
    // infinite loop to track heap use
#if defined(ENABLE_AUTOMATED_TX_RX_TEST)
    bool ptt = false;
    bool hasChangedModes = false;
#endif // ENABLE_AUTOMATED_TX_RX_TEST

    char buf[1024];
        
    /*ESP_LOGI(CURRENT_LOG_TAG, "heap free (8 bit): %d", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    ESP_LOGI(CURRENT_LOG_TAG, "heap free (32 bit): %d", heap_caps_get_free_size(MALLOC_CAP_32BIT));
    ESP_LOGI(CURRENT_LOG_TAG, "heap free (32 - 8 bit): %d", heap_caps_get_free_size(MALLOC_CAP_32BIT) - heap_caps_get_free_size(MALLOC_CAP_8BIT));
    ESP_LOGI(CURRENT_LOG_TAG, "heap free (internal): %d", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGI(CURRENT_LOG_TAG, "heap free (SPIRAM): %d", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(CURRENT_LOG_TAG, "heap free (DMA): %d", heap_caps_get_free_size(MALLOC_CAP_DMA));*/

#if defined(PRINT_PROCESS_STATS)
    print_real_time_stats(pdMS_TO_TICKS(1000));
#endif // defined(PRINT_PROCESS_STATS)

    //esp_timer_dump(stdout);
#if defined(ENABLE_AUTOMATED_TX_RX_TEST)
    ptt = !ptt;

    // Trigger PTT
    if (!hasChangedModes)
    {
        ezdv::audio::SetFreeDVModeMessage modeSetMessage(ezdv::audio::SetFreeDVModeMessage::FREEDV_700D);
        app->getFreeDVTask().post(&modeSetMessage);
        hasChangedModes = true;
    }

    if (ptt)
    {
        ezdv::driver::ButtonShortPressedMessage pressedMessage(ezdv::driver::PTT);
        app->getUITask().post(&pressedMessage);
    }
    else
    {
        ezdv::driver::ButtonReleasedMessage releasedMessage(ezdv::driver::PTT);
        app->getUITask().post(&releasedMessage);
    }
#endif // ENABLE_AUTOMATED_TX_RX_TEST

#endif // defined(TASK_TICK_DEBUGGING)
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

    ulp_num_cycles_with_gpio_on = 0;
    ulp_num_cycles_between_temp_checks = 0;

    // Note: mandatory before using DVTask.
    DVTask::Initialize();

    // Note: GPIO ISRs use per GPIO ISRs.
    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_LOWMED));
    
    app = new ezdv::App();
    assert(app != nullptr);

    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    /* not a wakeup from ULP, load the firmware */
    ESP_LOGI(CURRENT_LOG_TAG, "Wakeup reason: %d", cause);
    
    ESP_LOGI(CURRENT_LOG_TAG, "Power up mode: %" PRIu32, ulp_power_up_mode);
    
    /*if (cause != ESP_SLEEP_WAKEUP_ULP) {
        // Perform initial startup actions because we may not be fully ready yet
        app->start();

        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(CURRENT_LOG_TAG, "Starting power off application");
        app->sleep();
    }
    else*/
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Woken up via ULP, booting...");
        app->start();
    }   
}
