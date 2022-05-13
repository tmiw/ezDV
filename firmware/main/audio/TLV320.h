#ifndef RADIO__AUDIO__TLV320_H
#define RADIO__AUDIO__TLV320_H

#include "driver/i2s.h"
#include "driver/i2c.h"
#include "smooth/core/Task.h"
#include "smooth/core/util/FixedBuffer.h"
#include "smooth/core/timer/Timer.h"
#include "util/NamedQueue.h"
#include "Messaging.h"

#include "codec2_fifo.h"

// Driver for the TLV320 audio codec chip from Texas Instruments.

// TLV320 I2C address
#define TLV320_I2C_ADDRESS (0x18)

#define I2S_TIMER_INTERVAL_MS (10)
#define I2S_NUM_SAMPLES_PER_INTERVAL NUM_SAMPLES_PER_AUDIO_MESSAGE

namespace sm1000neo::audio
{
    class TLV320 : 
        public smooth::core::Task,
        public smooth::core::ipc::IEventListener<smooth::core::timer::TimerExpiredEvent>,
        public smooth::core::ipc::IEventListener<AudioDataMessage>
    {
    public:
        TLV320()
            : smooth::core::Task("TLV320", 4096, 10, std::chrono::milliseconds(1))
            , currentPage_(-1) // This will cause the page to be set to 0 on first I2C write.
            , timerExpiredQueue_(smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>::create(5, *this, *this))
            , audioDataInputQueue_(smooth::core::ipc::TaskEventQueue<AudioDataMessage>::create(25, *this, *this))
        {
            // Create output FIFOs so we can recombine both channels into one I2S stream.
            leftChannelOutFifo_ = codec2_fifo_create(I2S_NUM_SAMPLES_PER_INTERVAL * 5);
            assert(leftChannelOutFifo_ != nullptr);
            rightChannelOutFifo_ = codec2_fifo_create(I2S_NUM_SAMPLES_PER_INTERVAL * 5);
            assert(rightChannelOutFifo_ != nullptr);
            
            // Register input channel for use by other tasks.
            sm1000neo::util::NamedQueue::Add(AUDIO_OUT_PIPE_NAME, audioDataInputQueue_);
        }
        
        void event(const smooth::core::timer::TimerExpiredEvent& event) override;
        void event(const AudioDataMessage& event) override;
        
    protected:
        virtual void init();
        
    private:
        void setPage_(uint8_t page)
        {
    		i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    		i2c_master_start(cmd);
    		i2c_master_write_byte(cmd, (TLV320_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, I2C_MASTER_ACK);
            
            uint8_t buf[2] = { 0, page };
            
    		i2c_master_write(cmd, buf, sizeof(buf), true);
            i2c_master_stop(cmd);
            
            ESP_ERROR_CHECK(i2c_master_cmd_begin(I2C_NUM_0, cmd, 10/portTICK_PERIOD_MS));
            i2c_cmd_link_delete(cmd);
        }
        
        void setConfigurationOption_(uint8_t page, uint8_t reg, uint8_t val)
        {
            if (page != currentPage_)
            {
                setPage_(page);
            }
            
    		i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    		i2c_master_start(cmd);
    		i2c_master_write_byte(cmd, (TLV320_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, I2C_MASTER_ACK);
            
            uint8_t buf[2] = { reg, val };
            
    		i2c_master_write(cmd, buf, sizeof(buf), true);
            i2c_master_stop(cmd);
            
            ESP_ERROR_CHECK(i2c_master_cmd_begin(I2C_NUM_0, cmd, 10/portTICK_PERIOD_MS));
            i2c_cmd_link_delete(cmd);
        }
        
        uint8_t getConfigurationOption_(uint8_t page, uint8_t reg)
        {
            if (page != currentPage_)
            {
                setPage_(page);
            }
            
            uint8_t result;
    		i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    		i2c_master_start(cmd);
    		i2c_master_write_byte(cmd, (TLV320_I2C_ADDRESS << 1) | I2C_MASTER_READ, I2C_MASTER_ACK);
            i2c_master_write_byte(cmd, reg, I2C_MASTER_ACK);
            i2c_master_read_byte(cmd, &result, I2C_MASTER_NACK);
            i2c_master_stop(cmd);
            
            ESP_ERROR_CHECK(i2c_master_cmd_begin(I2C_NUM_0, cmd, 10/portTICK_PERIOD_MS));
            i2c_cmd_link_delete(cmd);
            
            return result;
        }
        
        int currentPage_;
        
        smooth::core::timer::TimerOwner readWriteTimer_;
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<smooth::core::timer::TimerExpiredEvent>> timerExpiredQueue_;
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<AudioDataMessage>> audioDataInputQueue_;
        
        struct FIFO* leftChannelOutFifo_;
        struct FIFO* rightChannelOutFifo_;
        
        void initializeI2S_();
        void initializeI2C_();
        void initializeResetGPIO_();
        
        void tlv320HardReset_();
        void tlv320ConfigureClocks_();
        void tlv320ConfigurePowerAndRouting_();
        void tlv320EnableAudio_();
    };

}


#endif // RADIO__AUDIO__TLV320_H
