#ifndef RADIO__AUDIO__TLV320_H
#define RADIO__AUDIO__TLV320_H

#include "driver/i2s.h"
#include "smooth/core/Task.h"
#include "smooth/core/io/i2c/Master.h"
#include "smooth/core/io/i2c/I2CMasterDevice.h"
#include "smooth/core/util/FixedBuffer.h"
#include "smooth/core/timer/Timer.h"
#include "util/NamedQueue.h"
#include "Messaging.h"

#include "codec2_fifo.h"

// Driver for the TLV320 audio codec chip from Texas Instruments.

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
        class I2CDevice : public smooth::core::io::i2c::I2CMasterDevice
        {
        public:
            I2CDevice(i2c_port_t port, uint8_t address, std::mutex& guard)
                : smooth::core::io::i2c::I2CMasterDevice(port, address, guard)
                , i2cAddress_(address)
            {
                //setPage_(0);
            }
            
            void setConfigurationOption(uint8_t page, uint8_t reg, uint8_t val)
            {
                /*if (page != currentPage_)
                {
                    setPage_(page);
                }
                
                std::vector<uint8_t> data {
                    reg,
                    val
                };
                write(i2cAddress_, data);*/
            }
            
            uint8_t getConfigurationOption(uint8_t page, uint8_t reg)
            {
                /*if (page != currentPage_)
                {
                    setPage_(page);
                }
                
                smooth::core::util::FixedBuffer<uint8_t, 1> result;
                assert(read(i2cAddress_, reg, result));
                
                return result[0];*/
                return 0;
            }
        private:
            uint8_t i2cAddress_;
            uint8_t currentPage_;
            
            void setPage_(uint8_t page)
            {
                std::vector<uint8_t> data {
                    (uint8_t)0x00,
                    page
                };
                write(address, data);
                currentPage_ = page;
            }
        };
        
        std::unique_ptr<smooth::core::io::i2c::Master> tlvMaster_;
        std::unique_ptr<I2CDevice> tlvDevice_;
        
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
