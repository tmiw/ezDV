#ifndef RADIO__AUDIO__TLV320_H
#define RADIO__AUDIO__TLV320_H

#include "driver/i2s.h"
#include "driver/i2c.h"
#include "smooth/core/Task.h"
#include "smooth/core/util/FixedBuffer.h"
#include "smooth/core/timer/Timer.h"
#include "util/NamedQueue.h"
#include "Constants.h"

#include "codec2_fifo.h"
#include "../codec/FreeDVTask.h"
#include "Messaging.h"

// Driver for the TLV320 audio codec chip from Texas Instruments.

// TLV320 I2C address
#define TLV320_I2C_ADDRESS (0x18)

namespace ezdv::audio
{
    class TLV320 : 
        public smooth::core::Task,
        public smooth::core::ipc::IEventListener<ezdv::audio::ChangeVolumeMessage>
    {
    public:
        TLV320()
            : smooth::core::Task("TLV320", 4096, 10, std::chrono::milliseconds(I2S_TIMER_INTERVAL_MS), 0)
            , currentPage_(-1) // This will cause the page to be set to 0 on first I2C write.
            , tlv320ControlQueue_(smooth::core::ipc::TaskEventQueue<ezdv::audio::ChangeVolumeMessage>::create(2, *this, *this))
        {
            // Create output FIFOs so we can recombine both channels into one I2S stream.
            leftChannelOutFifo_ = codec2_fifo_create(I2S_NUM_SAMPLES_PER_INTERVAL * 5);
            assert(leftChannelOutFifo_ != nullptr);
            rightChannelOutFifo_ = codec2_fifo_create(I2S_NUM_SAMPLES_PER_INTERVAL * 5);
            assert(rightChannelOutFifo_ != nullptr);
            
            ezdv::util::NamedQueue::Add(TLV320_CONTROL_PIPE_NAME, tlv320ControlQueue_);
        }
        
        virtual void tick() override;
        
        static TLV320& ThisTask()
        {
            return Task_;
        }
        
        void enqueueAudio(ezdv::audio::ChannelLabel channel, short* audioData, size_t length)
        {
            if (channel == ezdv::audio::ChannelLabel::LEFT_CHANNEL)
            {
                codec2_fifo_write(leftChannelOutFifo_, audioData, length);
            }
            else
            {
                codec2_fifo_write(rightChannelOutFifo_, audioData, length);
            }
        }
        
        void event(const ezdv::audio::ChangeVolumeMessage& event) override;
    protected:
        virtual void init();
        
    private:
        static TLV320 Task_;
        
        void setPage_(uint8_t page)
        {
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (TLV320_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, I2C_MASTER_ACK);
            
            uint8_t buf[2] = { 0, page };
            
            i2c_master_write(cmd, buf, sizeof(buf), I2C_MASTER_ACK);
            i2c_master_stop(cmd);
            
            ESP_ERROR_CHECK(i2c_master_cmd_begin(I2C_NUM_0, cmd, 1000/portTICK_PERIOD_MS));
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
            
            i2c_master_write(cmd, buf, sizeof(buf), I2C_MASTER_ACK);
            i2c_master_stop(cmd);
            
            ESP_ERROR_CHECK(i2c_master_cmd_begin(I2C_NUM_0, cmd, 1000/portTICK_PERIOD_MS));
            i2c_cmd_link_delete(cmd);
        }
        
        void setConfigurationOptionMultiple_(uint8_t page, uint8_t reg, uint8_t* val, uint8_t size)
        {
            if (page != currentPage_)
            {
                setPage_(page);
            }
            
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (TLV320_I2C_ADDRESS << 1) | I2C_MASTER_WRITE, I2C_MASTER_ACK);
            
            i2c_master_write_byte(cmd, reg, I2C_MASTER_ACK);
            i2c_master_write(cmd, val, size, I2C_MASTER_ACK);
            i2c_master_stop(cmd);
            
            ESP_ERROR_CHECK(i2c_master_cmd_begin(I2C_NUM_0, cmd, 1000/portTICK_PERIOD_MS));
            i2c_cmd_link_delete(cmd);
        }
        
        void setVolume_(ChannelLabel channel, int8_t vol);
        
        uint8_t getConfigurationOption_(uint8_t page, uint8_t reg)
        {
            if (page != currentPage_)
            {
                setPage_(page);
            }
            
            uint8_t result;            
            uint8_t regBuf[] = { reg };
            ESP_ERROR_CHECK(i2c_master_write_read_device(I2C_NUM_0, TLV320_I2C_ADDRESS, regBuf, 1, &result, 1, 1000/portTICK_PERIOD_MS));
            
            return result;
        }
        
        int currentPage_;
                
        struct FIFO* leftChannelOutFifo_;
        struct FIFO* rightChannelOutFifo_;
        std::shared_ptr<smooth::core::ipc::TaskEventQueue<ezdv::audio::ChangeVolumeMessage>> tlv320ControlQueue_;
        
        void initializeI2S_();
        void initializeI2C_();
        void initializeResetGPIO_();
        
        void tlv320HardReset_();
        void tlv320ConfigureClocks_();
        void tlv320ConfigureProcessingBlocks_();
        void tlv320ConfigurePower_();
        void tlv320ConfigureRoutingADC_();
        void tlv320ConfigureRoutingDAC_();
        void tlv320ConfigureAGC_();
    };

}


#endif // RADIO__AUDIO__TLV320_H
