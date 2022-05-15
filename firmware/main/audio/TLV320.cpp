#include "TLV320.h"
#include "driver/gpio.h"

// TLV320 reset pin GPIO
#define TLV320_RESET_GPIO GPIO_NUM_12

// TLV320 I2S interface GPIOs
#define TLV320_MCLK_GPIO GPIO_NUM_3
#define TLV320_BCLK_GPIO GPIO_NUM_46
#define TLV320_WCLK_GPIO GPIO_NUM_9
#define TLV320_DIN_GPIO GPIO_NUM_11
#define TLV320_DOUT_GPIO GPIO_NUM_10

// TLV320 I2C interface GPIOs
#define TLV320_SCL_GPIO GPIO_NUM_45
#define TLV320_SDA_GPIO GPIO_NUM_47
#define TLV320_SCK_FREQ_HZ (100000)

#define CURRENT_LOG_TAG ("TLV320")

using namespace sm1000neo::util;

namespace sm1000neo::audio
{
    void TLV320::init()
    {            
        ESP_LOGI(CURRENT_LOG_TAG, "initialize I2S and I2C");
        
        // Initialize I2S first so MCLK is available to the TLV320.
        initializeI2S_();
        initializeI2C_();
        
        // To begin, we need to hard reset the TLV320.
        ESP_LOGI(CURRENT_LOG_TAG, "reset TLV320");
        initializeResetGPIO_();
        tlv320HardReset_();
        
        // Enable required clocks.
        ESP_LOGI(CURRENT_LOG_TAG, "configure clocks");
        tlv320ConfigureClocks_();
        
        // Configure processing blocks
        ESP_LOGI(CURRENT_LOG_TAG, "configure processing blocks");
        tlv320ConfigureProcessingBlocks_();
        
        ESP_LOGI(CURRENT_LOG_TAG, "configure LDOs");
        tlv320ConfigurePower_();
        
        // Set power and I/O routing (ADC).
        ESP_LOGI(CURRENT_LOG_TAG, "configure routing for ADC");
        tlv320ConfigureRoutingADC_();
        
        // Set power and I/O routing (DAC).
        ESP_LOGI(CURRENT_LOG_TAG, "configure routing for DAC");
        tlv320ConfigureRoutingDAC_();
    }
    
    void TLV320::tick()
    {    
        short tempData[I2S_NUM_SAMPLES_PER_INTERVAL * 2];
        memset(tempData, 0, sizeof(tempData));
        
        //ESP_LOGI(CURRENT_LOG_TAG, "tick()");
        
        // Perform read from I2S. 
        size_t bytesRead = sizeof(tempData);
        ESP_ERROR_CHECK(i2s_read(I2S_NUM_0, tempData, sizeof(tempData), &bytesRead, portMAX_DELAY));
        
        // Send to Codec2
        AudioDataMessage leftChannelMessage;
        AudioDataMessage rightChannelMessage;
        for (auto index = 0; index < bytesRead / sizeof(short); index++)
        {
            leftChannelMessage.audioData[index] = tempData[2*index];
            rightChannelMessage.audioData[index] = tempData[2*index + 1];
        }
        leftChannelMessage.channel = AudioDataMessage::LEFT_CHANNEL;
        rightChannelMessage.channel = AudioDataMessage::RIGHT_CHANNEL;
        sm1000neo::codec::FreeDVTask& fdvTask = sm1000neo::codec::FreeDVTask::ThisTask();
        fdvTask.EnqueueAudio(AudioDataMessage::LEFT_CHANNEL, leftChannelMessage.audioData, bytesRead / 2 / sizeof(short));
        fdvTask.EnqueueAudio(AudioDataMessage::RIGHT_CHANNEL, rightChannelMessage.audioData, bytesRead / 2 / sizeof(short));
        
        // If we have available data in the FIFOs, send it out.
        short tempDataLeft[I2S_NUM_SAMPLES_PER_INTERVAL];
        short tempDataRight[I2S_NUM_SAMPLES_PER_INTERVAL];
        memset(tempDataLeft, 0, sizeof(tempDataLeft));
        memset(tempDataRight, 0, sizeof(tempDataRight));
        
        if (codec2_fifo_used(leftChannelOutFifo_) >= I2S_NUM_SAMPLES_PER_INTERVAL || 
            codec2_fifo_used(rightChannelOutFifo_) >= I2S_NUM_SAMPLES_PER_INTERVAL)
        {
            codec2_fifo_read(leftChannelOutFifo_, tempDataLeft, I2S_NUM_SAMPLES_PER_INTERVAL);
            codec2_fifo_read(rightChannelOutFifo_, tempDataRight, I2S_NUM_SAMPLES_PER_INTERVAL);

            for (auto index = 0; index < I2S_NUM_SAMPLES_PER_INTERVAL; index++)
            {
                tempData[2*index] = tempDataLeft[index];
                tempData[2*index + 1] = tempDataRight[index];
            }
            
            size_t bytesWritten = 0;
            ESP_ERROR_CHECK(i2s_write(I2S_NUM_0, tempData, sizeof(tempData), &bytesWritten, portMAX_DELAY));

            //ESP_LOGW(CURRENT_LOG_TAG, "transmitted %d bytes over I2S", bytesWritten);
        }
        else
        {
            ESP_LOGW(CURRENT_LOG_TAG, "no audio data for TX");
        }
    }
    
    void TLV320::initializeI2S_()
    {
        i2s_config_t tlv320_i2s_config;
        tlv320_i2s_config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_RX);
        tlv320_i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
        tlv320_i2s_config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
        tlv320_i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
        tlv320_i2s_config.dma_buf_count = 4;
        tlv320_i2s_config.dma_buf_len = I2S_NUM_SAMPLES_PER_INTERVAL;
        tlv320_i2s_config.use_apll = false;
        tlv320_i2s_config.mclk_multiple = I2S_MCLK_MULTIPLE_256;
        tlv320_i2s_config.chan_mask = I2S_CHANNEL_STEREO;
        tlv320_i2s_config.total_chan = 2;
        
        // Request 8K sample rate @ 16 bits to reduce the amount of work we need
        // to do internally to up/downconvert.
        tlv320_i2s_config.sample_rate = 8000; 
        tlv320_i2s_config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
        tlv320_i2s_config.bits_per_chan = I2S_BITS_PER_CHAN_16BIT;

        // Set I2S pins.
        i2s_pin_config_t pin_config;
        pin_config.bck_io_num = TLV320_BCLK_GPIO;
        pin_config.ws_io_num = TLV320_WCLK_GPIO;
        pin_config.data_out_num = TLV320_DOUT_GPIO;
        pin_config.data_in_num = TLV320_DIN_GPIO;
        pin_config.mck_io_num = TLV320_MCLK_GPIO;
        
        ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM_0, &tlv320_i2s_config, 0, NULL));
        ESP_ERROR_CHECK(i2s_set_pin(I2S_NUM_0, &pin_config));
    }
    
    void TLV320::initializeI2C_()
    {
    	i2c_config_t conf;
        memset(&conf, 0, sizeof(i2c_config_t));
    	conf.mode = I2C_MODE_MASTER;
    	conf.sda_io_num = TLV320_SDA_GPIO;
    	conf.scl_io_num = TLV320_SCL_GPIO;
    	conf.sda_pullup_en = GPIO_PULLUP_DISABLE;
    	conf.scl_pullup_en = GPIO_PULLUP_DISABLE;
    	conf.master.clk_speed = TLV320_SCK_FREQ_HZ;
    	i2c_param_config(I2C_NUM_0, &conf);

    	i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);
    }
    
    void TLV320::initializeResetGPIO_()
    {
        gpio_intr_disable(TLV320_RESET_GPIO);
        gpio_set_direction(TLV320_RESET_GPIO, GPIO_MODE_OUTPUT);
        gpio_set_pull_mode(TLV320_RESET_GPIO, GPIO_FLOATING);
        gpio_set_level(TLV320_RESET_GPIO, 1); // active low
    }
    
    void TLV320::tlv320HardReset_()
    {
        // TLV320's reset line must be held low for 10ns for the reset
        // to start. We also have to wait for 1ms after the reset
        // goes high for everything to reset properly. See section 3.1-3.2 
        // of the Application Reference Guide 
        // (https://www.ti.com/lit/an/slaa408a/slaa408a.pdf).
        gpio_set_level(TLV320_RESET_GPIO, 0);
        ets_delay_us(1);
        gpio_set_level(TLV320_RESET_GPIO, 1);
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    
    void TLV320::tlv320ConfigureClocks_()
    {
        // Clock calculations for 8K sample rate per guide at 
        // https://www.ti.com/lit/an/slaa404c/slaa404c.pdf
                
        // AOSR = 128
        // DOSR = 128
        // ADC_FS = 8K
        // DAC_FS = 8K
        // ADC_MOD_CLK = AOSR * ADC_FS = 128 * 8000 = 1.024 MHz <= 6.758 MHz
        // DAC_MOD_CLK = DOSR * DAC_FS = 128 * 8000 = 1.024 MHz <= 6.758 MHz
        
        // ADC Processing Block = PRB_R1
        // DAC Processing Block = PRB_P1
        // MADC = 2
        // MDAC = 2
        // ADC_CLK = MADC * ADC_MOD_CLK = 2 * 1.024 MHz = 2.048 MHz
        // DAC_CLK = MDAC * DAC_MOD_CLK = 2 * 1.024 MHz = 2.048 MHz
        // (MADC * AOSR) / 32 = 256 / 32 = 8 >= RC(R1) = 6
        // (MDAC * DOSR) / 32 = 256 / 32 = 8 >= RC(P1) = 8
        // ADC_CLK <= 55.296 MHz
        // DAC_CLK <= 55.296 MHz
        
        // NADC = 40
        // NDAC = 40
        // CODEC_CLKIN = NADC * ADC_CLK = NDAC * DAC_CLK = 81.92 MHz
        // CODEC_CLKIN <= 137MHz
        // CODEC_CLKIN from PLL_CLK
        
        // MCLK is 2.048MHz (8000 * 256)
        // PLL_CLK = MCLK * R * J.D/P
        // 81.92 MHz = 2.048 * 1 * 40.0000 / 1
        // P = 1, R = 1, J = 40, D = 0 
        
        uint8_t pllOpts[] = {
            // Set CODEC_CLKIN to PLL and use MCLK for PLL
            // (Page 0, register 4)
            (0 << 2) | 0b11,
            
            // Set PLL P = 1, R = 1, J = 40, D = 0, power up PLL
            // (Page 0, registers 5-8)
            (1 << 7) | (0b001 << 4) | (0b001),  // P, R, power up
            40, // J
            0, // D[MSB]
            0 // D[LSB]
        };
        setConfigurationOptionMultiple_(0, 4, pllOpts, 5);
        
        // Wait 10ms for PLL to become available
        // (Section 2.7.1, "TLV320AIC3254 Application Reference Guide")
        vTaskDelay(pdMS_TO_TICKS(10));
        
        // Set NADC = 40, MADC = 2. Power up.
        // (Page 0, registers 11 and 12)
        uint8_t adcOpts[] = {
            (1 << 7) | 40,
            (1 << 7) | 2
        };
        setConfigurationOptionMultiple_(0, 11, adcOpts, 2);
        
        // Set NDAC = 40, MDAC = 2. Power up.
        // (Page 0, registers 18 and 19)
        uint8_t dacOpts[] = {
            (1 << 7) | 40,
            (1 << 7) | 2
        };
        setConfigurationOptionMultiple_(0, 18, dacOpts, 2);
        
        // Program DOSR to 128 (Page 0, registers 13-14)
        uint8_t dosr[] = {
            0,
            128
        };
        setConfigurationOptionMultiple_(0, 13, dosr, 2);
        
        // Program AOSR to 128 (Page 0, register 20).
        setConfigurationOption_(0, 20, 128);
        
        // Set I2S word size to 16 bits (Page 0, register 27)
        setConfigurationOption_(0, 27, 0);
    }
    
    void TLV320::tlv320ConfigurePower_()
    {
        // Power up AVDD LDO
        setConfigurationOption_(1, 2, (1 << 3) | (1 << 0));
        
        // Disable weak AVDD in presence of external AVDD supply (Page 1, register 1)
        setConfigurationOption_(1, 1, (1 << 3));
        
        // AVDD/DVDD 1.72V, AVDD LDO powered up (Page 1, register 2)
        setConfigurationOption_(1, 2, (0 << 3) | (1 << 0));
        
        // Set full chip common mode to 0.9V
        // HP output CM = 1.65V
        // HP driver supply = LDOin voltage
        // Line output CM = 1.65V
        // Line output supply = LDOin voltage
        // (Page 1, register 10)
        setConfigurationOption_(1, 10, (3 << 4) | (1 << 3) | (1 << 1) | (1 << 0));
        
        // Set ADC PTM to PTM_R4 (Page 1, register 61)
        setConfigurationOption_(1, 61, 0);
        
        // Set DAC PTM to PTM_R3 (Page 1, registers 3-4)
        // Note: PTM_R4 requires >= 20 bits for I2S, hence not used here.
        uint8_t dacPtm[] = {
            0,
            0
        };
        setConfigurationOptionMultiple_(1, 3, dacPtm, 2);
        
        // Set input powerup time to 3.1ms (Page 1, register 71)
        setConfigurationOption_(1, 71, 0b110001);
        
        // REF will power up in 40ms (Page 1, register 123)
        setConfigurationOption_(1, 123, 1);
    }
    
    void TLV320::tlv320ConfigureProcessingBlocks_()
    {
        // Set ADC_PRB and DAC_PRB to P1 and R1 (Page 0, registers 60-61).
        uint8_t prb[] = {
            1,
            1
        };
        setConfigurationOptionMultiple_(0, 60, prb, 2);
    }
    
    void TLV320::tlv320ConfigureRoutingADC_()
    {
        // Enable 2.5V mic bias on headset jack using LDOIN (Page 1, register 51)
        setConfigurationOption_(1, 51, (1 << 6) | (0b10 << 4) | (1 << 3));
        
        // Set ADC routing: IN1_L left channel, IN1_R right channel,
        // 20kohm impedence (Page 1, registers 52, 54, 55, 57)
        setConfigurationOption_(1, 52, 1 << 7);
        setConfigurationOption_(1, 54, 1 << 7);
        setConfigurationOption_(1, 55, 1 << 7);
        setConfigurationOption_(1, 57, 1 << 7);
        
        // Unmute PGAs, gain = 6dB due to 20k impedence
        // (Page 1, registers 59 and 60)
        setConfigurationOption_(1, 59, 0x0c);
        setConfigurationOption_(1, 60, 0x0c);
        
        // Power on ADC (Page 0, register 81)
        // Unmute ADC (Page 0, register 82)
        uint8_t adc[] = {
            (1 << 7) | (1 << 6),
            0
        };
        setConfigurationOptionMultiple_(0, 81, adc, 2);
    }
    
    void TLV320::tlv320ConfigureRoutingDAC_()
    {
        // 6kohm depop, N = 5.0, 50ms soft start (Page 1, register 20)
        setConfigurationOption_(1, 20, (1 << 6) | (0b1001 << 2) | (1 << 0));
        
        // Set DAC routing: HPL, HPR come from DAC
        // (Page 1, registers 12 and 13)
        setConfigurationOption_(1, 12, 1 << 3);
        setConfigurationOption_(1, 13, 1 << 3);
        
        // Power up HPL and HPR
        // (Page 1, register 9)
        setConfigurationOption_(1, 9, (1 << 5) | (1 << 4));
        
        // Unmute HPL and HPR, gain = -6dB HPL, -6 dB HPR
        // (Page 1, registers 16 and 17)
        setConfigurationOption_(1, 16, -6 & 0b00111111);
        setConfigurationOption_(1, 17, -6 & 0b00111111);
        
        // Wait until gain fully applied
        // (Page 1, register 63)
        uint8_t gainRegVal = 0;
        int count = 0;
        do
        {
            vTaskDelay(pdMS_TO_TICKS(1));
            gainRegVal = getConfigurationOption_(1, 63);
        } while ((count++ < 50) && (gainRegVal & (11 << 6)) == 0);
        
        // Power on DAC (Page 0, register 63)
        // Unmute DAC (Page 0, register 64)
        uint8_t dac[] = {
            (1 << 7) | (1 << 6) | (1 << 4) | (1 << 2) | (1 << 1),
            0
        };
        setConfigurationOptionMultiple_(0, 63, dac, 2);
    }
}
