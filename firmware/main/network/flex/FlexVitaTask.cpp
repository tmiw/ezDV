/* 
 * This file is part of the ezDV project (https://github.com/tmiw/ezDV).
 * Copyright (c) 2023 Mooneer Salem
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

#include <cmath>
#include <unistd.h>

#include "FlexVitaTask.h"
#include "FlexKeyValueParser.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_dsp.h"

#include "codec2_fifo.h"
#include "codec2_fdmdv.h"

#include "SampleRateConverter.h"

#define MAX_VITA_PACKETS (200)
#define MAX_VITA_SAMPLES (42) /* 5.25ms/block @ 8000 Hz */
#define MAX_VITA_SAMPLES_TO_RESAMPLE (MAX_VITA_SAMPLES * FDMDV_OS_24) /* Must be less than the max size of the VITA packet (180 two channel samples) */
#define VITA_SAMPLES_TO_SEND MAX_VITA_SAMPLES_TO_RESAMPLE
#define MIN_VITA_PACKETS_TO_SEND (4)
#define MAX_VITA_PACKETS_TO_SEND (10)
#define US_OF_AUDIO_PER_VITA_PACKET (5250)
#define VITA_IO_TIME_INTERVAL_US (US_OF_AUDIO_PER_VITA_PACKET * MIN_VITA_PACKETS_TO_SEND) /* Time interval between subsequent sends or receives */
#define MAX_JITTER_US (500) /* Corresponds to +/- the maximum amount VITA_IO_TIME_INTERVAL_US should vary by. */

#define CURRENT_LOG_TAG "FlexVitaTask"

namespace ezdv
{

namespace network
{
    
namespace flex
{
    
static float tx_scale_factor = std::exp(6.2f/20.0f * std::log(10.0f));

FlexVitaTask::FlexVitaTask()
    : DVTask("FlexVitaTask", 16, 4096, 1, 512)
    , audio::AudioInput(2, 2)
    , packetReadTimer_(this, this, &FlexVitaTask::readPendingPackets_, VITA_IO_TIME_INTERVAL_US, "FlexVitaPacketReadTimer")
    , packetWriteTimer_(this, this, &FlexVitaTask::sendAudioOut_, VITA_IO_TIME_INTERVAL_US, "FlexVitaPacketWriteTimer")
    , socket_(-1)
    , rxStreamId_(0)
    , txStreamId_(0)
    , audioSeqNum_(0)
    , currentTime_(0)
    , timeFracSeq_(0)
    , audioEnabled_(false)
    , isTransmitting_(false)
    , inputCtr_(0)
    , lastVitaGenerationTime_(0)
    , minPacketsRequired_(0)
    , timeBeyondExpectedUs_(0)
{
    registerMessageHandler(this, &FlexVitaTask::onFlexConnectRadioMessage_);
    registerMessageHandler(this, &FlexVitaTask::onReceiveVitaMessage_);
    registerMessageHandler(this, &FlexVitaTask::onSendVitaMessage_);
    registerMessageHandler(this, &FlexVitaTask::onEnableReportingMessage_);
    registerMessageHandler(this, &FlexVitaTask::onDisableReportingMessage_);
    registerMessageHandler(this, &FlexVitaTask::onRequestRxMessage_);
    registerMessageHandler(this, &FlexVitaTask::onRequestTxMessage_);

    downsamplerInBuf_ = (float*)heap_caps_calloc((MAX_VITA_SAMPLES * FDMDV_OS_24 + FDMDV_OS_TAPS_24K), sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
    assert(downsamplerInBuf_ != nullptr);
    downsamplerOutBuf_ = (short*)heap_caps_calloc(MAX_VITA_SAMPLES, sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
    assert(downsamplerOutBuf_ != nullptr);
    upsamplerInBuf_ = (short*)heap_caps_calloc((MAX_VITA_SAMPLES + FDMDV_OS_TAPS_24_8K), sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
    assert(upsamplerInBuf_ != nullptr);
    upsamplerOutBuf_ = (float*)heap_caps_calloc((MAX_VITA_SAMPLES * FDMDV_OS_24), sizeof(float), MALLOC_CAP_INTERNAL | MALLOC_CAP_32BIT);
    assert(upsamplerOutBuf_ != nullptr);

    packetArray_ = (vita_packet*)heap_caps_calloc(MAX_VITA_PACKETS, sizeof(vita_packet), MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
    assert(packetArray_ != nullptr);
    packetIndex_ = 0;
}

FlexVitaTask::~FlexVitaTask()
{
    disconnect_();
    
    heap_caps_free(downsamplerInBuf_);
    heap_caps_free(upsamplerInBuf_);
    heap_caps_free(downsamplerOutBuf_);
    heap_caps_free(upsamplerOutBuf_);
    heap_caps_free(packetArray_);
}

void FlexVitaTask::onTaskStart_()
{
    openSocket_();
}

void FlexVitaTask::onTaskSleep_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "Sleeping task");
    disconnect_();
}

void FlexVitaTask::onTaskTick_()
{
    // empty
}

void FlexVitaTask::generateVitaPackets_(audio::AudioInput::ChannelLabel channel, uint32_t streamId)
{
    auto fifo = getAudioInput(channel);

    auto currentTimeInMicroseconds = esp_timer_get_time();
    auto timeSinceLastPacketSend = currentTimeInMicroseconds - lastVitaGenerationTime_;
    lastVitaGenerationTime_ = currentTimeInMicroseconds;

    // If we're starved of audio, don't even bother going through the rest of the logic right now
    // and reset state back to the point right when we started.
    /*if (codec2_fifo_used(fifo) < MAX_VITA_SAMPLES_TO_RESAMPLE * minPacketsRequired_)
    {
        if (isTransmitting_) ESP_LOGW(CURRENT_LOG_TAG, "Not enough audio samples to process/send packets (minimum packets needed: %d)!", minPacketsRequired_);
        minPacketsRequired_ = 0;
        timeBeyondExpectedUs_ = 0;
        return;
    }*/

    if (isTransmitting_ && (
        timeSinceLastPacketSend >= (VITA_IO_TIME_INTERVAL_US + MAX_JITTER_US) ||
        timeSinceLastPacketSend <= (VITA_IO_TIME_INTERVAL_US - MAX_JITTER_US)))
    {
        ESP_LOGW(
            CURRENT_LOG_TAG, 
            "Packet TX jitter is a bit high (time = %" PRIu64 ", expected: %d-%d)", 
            timeSinceLastPacketSend,
            VITA_IO_TIME_INTERVAL_US - MAX_JITTER_US,
            VITA_IO_TIME_INTERVAL_US + MAX_JITTER_US);
    }

    // Determine the number of extra packets we need to send this go-around.
    // This is since it can take a bit longer than the timer interval to 
    // actually enter this method (depending on what else is going on in the
    // system at the time).
    int addedExtra = 0;
    auto packetsToSend = MIN_VITA_PACKETS_TO_SEND * timeSinceLastPacketSend / VITA_IO_TIME_INTERVAL_US;
    if (packetsToSend == 0 && minPacketsRequired_ == 0)
    {
        // We're executing too quickly (or there are a bunch of events piled up that
        // need to be worked through). We'll wait until the next time we're actually
        // supposed to execute.
        ESP_LOGW(CURRENT_LOG_TAG, "Executing send handler too quickly!");
        return;
    }
    else
    {
        //ESP_LOGI(CURRENT_LOG_TAG, "In the previous interval, %" PRIu64 " packets should have gone out (time since last send = %" PRIu64 ")", packetsToSend, timeSinceLastPacketSend);
        minPacketsRequired_ += packetsToSend;
        timeBeyondExpectedUs_ += timeSinceLastPacketSend % VITA_IO_TIME_INTERVAL_US;
    }

    while (timeBeyondExpectedUs_ >= (US_OF_AUDIO_PER_VITA_PACKET >> 1))
    {
        addedExtra = 1;
        minPacketsRequired_++;
        timeBeyondExpectedUs_ -= US_OF_AUDIO_PER_VITA_PACKET;
    }

    if (minPacketsRequired_ <= 0)
    {
        minPacketsRequired_ = 0;
        timeBeyondExpectedUs_ = 0;
    }

    //ESP_LOGI(CURRENT_LOG_TAG, "Packets to be sent this time: %d", minPacketsRequired_);
    int ctr = MAX_VITA_PACKETS_TO_SEND;
    while(minPacketsRequired_ > 0 && ctr > 0 && codec2_fifo_read(fifo, &upsamplerInBuf_[FDMDV_OS_TAPS_24_8K], MAX_VITA_SAMPLES) == 0)
    {
        minPacketsRequired_--;
        ctr--;

        if (!audioEnabled_)
        {
            // Skip sending audio to SmartSDR if the user isn't using us yet.
            continue;
        }
        
        // Upsample to 24K floats.
        fdmdv_8_to_24(upsamplerOutBuf_, &upsamplerInBuf_[FDMDV_OS_TAPS_24_8K], MAX_VITA_SAMPLES);

        // Scale output audio as SmartSDR is a lot quieter than expected otherwise.
        dsps_mulc_f32(
            upsamplerOutBuf_,
            upsamplerOutBuf_,
            MAX_VITA_SAMPLES_TO_RESAMPLE,
            tx_scale_factor,
            1,
            1
        );

        uint32_t* dataPtr = (uint32_t*)&upsamplerOutBuf_[0];
        uint32_t masks[] = 
        {
            0x000000ff,
            0x00ff0000,
            0x0000ff00,
            0xff000000
        };

        // Get free packet
        vita_packet* packet = &packetArray_[packetIndex_++];
        assert(packet != nullptr);
        if (packetIndex_ == MAX_VITA_PACKETS)
        {
            packetIndex_ = 0;
        }
        uint32_t* ptrOut = (uint32_t*)packet->if_samples;

        int optimizedNumToSend = MAX_VITA_SAMPLES_TO_RESAMPLE & 0xFFFFFFFC; // We only operate in blocks of 4 samples.
        for (int i = 0; i < optimizedNumToSend >> 2; i++)
        {
            uint32_t* ptrMasks = masks;

            // Assumption: ptrIn is 16 byte aligned.
            asm volatile(
                "ld.qr q0, %1, 0\n"              // Load audio sample into q0

                "movi a10, 24\n"
                "wsr a10, sar\n"                 // Load 24 into sar register
                "mv.qr q5, q0\n"                 // Copy q0 into q5
                "mv.qr q6, q0\n"                 // Copy q0 into q6
                "ee.vldbc.32.ip q1, %2, 4\n"     // Load 0x000000ff 4 times into q1
                "ee.vsr.32 q5, q5\n"             // Shift all four values in q5 right 24 bits
                "ee.andq q1, q1, q5\n"           // q1 = q5 & 0x000000ff
                "ee.vldbc.32.ip q4, %2, 4\n"     // Load 0xff000000 4 times into q4
                "ee.vsl.32 q6, q6\n"             // Shift all four values in q6 left 24 bits
                "ee.andq q4, q4, q6\n"           // q4 = q4 & 0xff000000

                "movi a10, 8\n"
                "wsr a10, sar\n"                 // Load 8 into sar register
                "mv.qr q5, q0\n"                 // Copy q0 into q5
                "mv.qr q6, q0\n"                 // Copy q0 into q6
                "ee.vsr.32 q5, q5\n"             // Shift all four values in q5 right 8 bits
                "ee.vldbc.32.ip q3, %2, 4\n"     // Load 0x0000ff00 4 times into q3
                "ee.andq q3, q3, q5\n"           // q3 = q5 & 0x0000ff00
                "ee.vldbc.32.ip q2, %2, 4\n"     // Load 0x00ff0000 4 times into q2
                "ee.vsl.32 q6, q6\n"             // Shift all four values in q6 left 8 bits
                "ee.andq q2, q2, q6\n"           // q2 = q6 & 0x00ff0000

                "ee.orq q0, q1, q2\n"            // q0 = q1 | q2
                "ee.orq q0, q0, q3\n"            // q0 = q0 | q3
                "ee.orq q0, q0, q4\n"            // q0 = q0 | q4

                "mv.qr q1, q0\n"                 // Copy q0 into q1
                "ee.vzip.32 q0, q1\n"            // Interleave each word of q0 and q1 together

                "st.qr q0, %0, 0\n"              // Save first word to ptrOut
                "st.qr q1, %0, 16\n"             // Save second word to ptrOut
                "addi %0, %0, 32\n"              // Add 32 to ptrOut address (8 samples)
                "addi %1, %1, 16\n"              // Add 16 to dataPtr address (4 samples)
                : "=r"(ptrOut), "=r"(dataPtr), "=r"(ptrMasks)
                : "0"(ptrOut), "1"(dataPtr), "2"(ptrMasks)
                : "a10", "memory"
            );
        }

        // Get the remaining ones that we couldn't get to with the optimized logic above.
        while (dataPtr < (uint32_t*)&upsamplerOutBuf_[MAX_VITA_SAMPLES_TO_RESAMPLE])
        {
            uint32_t tmp = htonl(*dataPtr);
            *ptrOut++ = tmp;
            *ptrOut++ = tmp;
            dataPtr++;
        }
                
        // Fil in packet with data
        packet->packet_type = VITA_PACKET_TYPE_IF_DATA_WITH_STREAM_ID;
        packet->stream_id = streamId;
        packet->class_id = AUDIO_CLASS_ID;
        packet->timestamp_type = audioSeqNum_++;

        size_t packet_len = VITA_PACKET_HEADER_SIZE + VITA_SAMPLES_TO_SEND * 2 * sizeof(float);

        //  XXX Lots of magic numbers here!
        packet->timestamp_type = 0x50u | (packet->timestamp_type & 0x0Fu);
        assert(packet_len % 4 == 0);
        packet->length = htons(packet_len >> 2); // Length is in 32-bit words, note there are two channels

        packet->timestamp_int = time(NULL);
        /*if (packet->timestamp_int != currentTime_)
        {
            timeFracSeq_ = 0;
        }*/
        
        packet->timestamp_frac = __builtin_bswap64(audioSeqNum_ - 1); // timeFracSeq_++;
        currentTime_ = packet->timestamp_int;

        SendVitaMessage message(packet, packet_len);
        post(&message);
    }

    minPacketsRequired_ -= addedExtra;
}

void FlexVitaTask::openSocket_()
{
    // Bind socket so we can at least get discovery packets.
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == -1)
    {
        auto err = errno;
        ESP_LOGE(CURRENT_LOG_TAG, "Got socket error %d (%s) while creating socket", err, strerror(err));
    }
    assert(socket_ != -1);

    // Listen on our hardcoded VITA port
    struct sockaddr_in ourSocketAddress;
    memset((char *) &ourSocketAddress, 0, sizeof(ourSocketAddress));

    ourSocketAddress.sin_family = AF_INET;
    ourSocketAddress.sin_port = htons(VITA_PORT);
    ourSocketAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    
    auto rv = bind(socket_, (struct sockaddr*)&ourSocketAddress, sizeof(ourSocketAddress));
    if (rv == -1)
    {
        auto err = errno;
        ESP_LOGE(CURRENT_LOG_TAG, "Got socket error %d (%s) while binding", err, strerror(err));
    }
    assert(rv != -1);

    fcntl (socket_, F_SETFL , O_NONBLOCK);

    // The below tells ESP-IDF to transmit on this socket using Wi-Fi voice priority.
    // This also implicitly disables TX AMPDU for this socket. In testing, not having
    // this worked a lot better than having it, so it's disabled for now. Perhaps in
    // the future we can play with this again (perhaps with a lower priority level that
    // will in fact use AMPDU?)
#if 0
    const int precedenceVI = 6;
    const int precedenceOffset = 7;
    int priority = (precedenceVI << precedenceOffset);
    setsockopt(socket_, IPPROTO_IP, IP_TOS, &priority, sizeof(priority));
#endif // 0

    minPacketsRequired_ = MIN_VITA_PACKETS_TO_SEND;
    timeBeyondExpectedUs_ = 0;
    lastVitaGenerationTime_ = esp_timer_get_time();

    packetReadTimer_.start();
    packetWriteTimer_.start();

    inputCtr_ = 0;}

void FlexVitaTask::disconnect_()
{
    if (socket_ > 0)
    {
        packetReadTimer_.stop();
        packetWriteTimer_.stop();
        close(socket_);
        socket_ = -1;
        
        rxStreamId_ = 0;
        txStreamId_ = 0;
        audioSeqNum_ = 0;
        currentTime_ = 0;
        timeFracSeq_ = 0;
        inputCtr_ = 0;
        packetIndex_ = 0;
    }
}

void FlexVitaTask::readPendingPackets_(DVTimer*)
{
    fd_set readSet;
    struct timeval tv = {0, 0};
    
    FD_ZERO(&readSet);
    FD_SET(socket_, &readSet);
    
    // Process if there are pending datagrams in the buffer
    int ctr = MAX_VITA_PACKETS_TO_SEND;
    while (ctr-- > 0 && select(socket_ + 1, &readSet, nullptr, nullptr, &tv) > 0)
    {
        vita_packet* packet = &packetArray_[packetIndex_++];
        assert(packet != nullptr);

        if (packetIndex_ == MAX_VITA_PACKETS)
        {
            packetIndex_ = 0;
        }
        
        auto rv = recv(socket_, (char*)packet, sizeof(vita_packet), 0);
        if (rv > 0)
        {
            // Queue up packet for future processing.
            ReceiveVitaMessage message(packet, rv);
            post(&message);
        }
        
        // Reinitialize the read set for the next pass.
        FD_ZERO(&readSet);
        FD_SET(socket_, &readSet);
    }
}

void FlexVitaTask::sendAudioOut_(DVTimer*)
{
    // Generate packets for both RX and TX.
    if (rxStreamId_ && !isTransmitting_)
    {
        generateVitaPackets_(audio::AudioInput::USER_CHANNEL, rxStreamId_);
    }
    else if (rxStreamId_)
    {
        // Clear FIFO if we're not in the right state. This is so that we
        // don't end up with audio packets going to the wrong place
        // (i.e. UI beeps being transmitted along with the FreeDV signal).
        auto fifo = getAudioInput(audio::AudioInput::USER_CHANNEL);
        short tmpBuf[MAX_VITA_SAMPLES];
        while(codec2_fifo_read(fifo, tmpBuf, MAX_VITA_SAMPLES) == 0)
        {
            // empty
        }
    }
    
    if (txStreamId_ && isTransmitting_)
    {
        generateVitaPackets_(audio::AudioInput::RADIO_CHANNEL, txStreamId_);
    }
    else if (txStreamId_)
    {
        // Clear FIFO if we're not in the right state. This is so that we
        // don't end up with audio packets going to the wrong place
        // (i.e. UI beeps being transmitted along with the FreeDV signal).
        auto fifo = getAudioInput(audio::AudioInput::RADIO_CHANNEL);
        short tmpBuf[MAX_VITA_SAMPLES];
        while(codec2_fifo_read(fifo, tmpBuf, MAX_VITA_SAMPLES) == 0)
        {
            // empty
        }
    }
}

void FlexVitaTask::onFlexConnectRadioMessage_(DVTask* origin, FlexConnectRadioMessage* message)
{
    ip_ = message->ip;
    
    radioAddress_.sin_addr.s_addr = inet_addr(ip_.c_str());
    radioAddress_.sin_family = AF_INET;
    radioAddress_.sin_port = htons(4993); // hardcoded as per Flex documentation
    
    ESP_LOGI(CURRENT_LOG_TAG, "Connected to radio successfully");
}

void FlexVitaTask::onReceiveVitaMessage_(DVTask* origin, ReceiveVitaMessage* message)
{
    vita_packet* packet = message->packet;
    
    // Make sure packet is long enough to inspect for VITA header info.
    if (message->length < VITA_PACKET_HEADER_SIZE)
        goto cleanup;

    // Make sure packet is from the radio.
    if((packet->class_id & VITA_OUI_MASK) != FLEX_OUI)
        goto cleanup;

    // Look for discovery packets
    if (packet->stream_id == DISCOVERY_STREAM_ID && packet->class_id == DISCOVERY_CLASS_ID)
    {
        std::stringstream ss((char*)packet->raw_payload);
        auto parameters = FlexKeyValueParser::GetCommandParameters(ss);

        auto radioFriendlyName = parameters["nickname"] + " (" + parameters["callsign"] + ")";
        auto radioIp = parameters["ip"];
        
        ESP_LOGI(CURRENT_LOG_TAG, "Discovery: found radio %s at IP %s", radioFriendlyName.c_str(), radioIp.c_str());
        
        FlexRadioDiscoveredMessage discoveryMessage((char*)radioFriendlyName.c_str(), (char*)radioIp.c_str());
        publish(&discoveryMessage);
        
        goto cleanup;
    }
    
    switch(packet->stream_id & STREAM_BITS_MASK) 
    {
        case STREAM_BITS_WAVEFORM | STREAM_BITS_IN:
        {
            unsigned long payload_length = ((htons(packet->length) * sizeof(uint32_t)) - VITA_PACKET_HEADER_SIZE);
            /*if(payload_length != packet->length - VITA_PACKET_HEADER_SIZE) 
            {
                ESP_LOGW(CURRENT_LOG_TAG, "VITA header size doesn't match bytes read from network (%lu != %u - %u) -- %u\n", payload_length, packet->length, VITA_PACKET_HEADER_SIZE, sizeof(struct vita_packet));
                goto cleanup;
            }*/

            audio::AudioInput::ChannelLabel channel = audio::AudioInput::RADIO_CHANNEL;
            if (!(htonl(packet->stream_id) & 0x0001u)) 
            {
                // Packet contains receive audio from radio.
                rxStreamId_ = packet->stream_id;
            } 
            else 
            {
                // Packet contains transmit audio from user's microphone.
                txStreamId_ = packet->stream_id;
                channel = audio::AudioInput::USER_CHANNEL;
            }
            
            // Downconvert to 8K sample rate.
            unsigned int num_samples = payload_length >> 2; // / sizeof(uint32_t);
            unsigned int half_num_samples = num_samples >> 1;

            int i = 0;
            auto fifo = getAudioOutput(channel);
            while (fifo != nullptr && i < half_num_samples)
            {
                uint32_t temp = ntohl(packet->if_samples[i << 1]);
                downsamplerInBuf_[FDMDV_OS_TAPS_24K + (inputCtr_++)] = *(float*)&temp;
                i++;

                if (inputCtr_ == MAX_VITA_SAMPLES * FDMDV_OS_24)
                {
                    inputCtr_ = 0;
                    fdmdv_24_to_8(downsamplerOutBuf_, &downsamplerInBuf_[FDMDV_OS_TAPS_24K], MAX_VITA_SAMPLES);
            
                    // Queue on respective FIFO.
                    // Note: may be null during voice keyer operation
                    codec2_fifo_write(fifo, downsamplerOutBuf_, MAX_VITA_SAMPLES);
                }
            }            
            break;
        }
        default:
            ESP_LOGW(CURRENT_LOG_TAG, "Undefined stream in %lx", htonl(packet->stream_id));
            break;
    }

cleanup:
    // no cleanup needed
}

void FlexVitaTask::onSendVitaMessage_(DVTask* origin, SendVitaMessage* message)
{
    const int MAX_RETRY_TIME_MS = 50;
    
    auto packet = message->packet;
    assert(packet != nullptr);

    if (socket_ > 0)
    {
        auto startTime = esp_timer_get_time();
        int tries = 1;
        int rv = sendto(socket_, (char*)packet, message->length, 0, (struct sockaddr*)&radioAddress_, sizeof(radioAddress_));
        auto totalTimeMs = (esp_timer_get_time() - startTime)/1000;
        while (rv == -1 && totalTimeMs < MAX_RETRY_TIME_MS)
        {
            auto err = errno;
            if (err == ENOMEM)
            {
                // Wait a bit and try again; the Wi-Fi subsystem isn't ready yet.
                vTaskDelay(1);
                tries++;
                rv = sendto(socket_, (char*)packet, message->length, 0, (struct sockaddr*)&radioAddress_, sizeof(radioAddress_));
                continue;
            }
            else
            {
                // TBD: close/reopen connection
                ESP_LOGE(
                    CURRENT_LOG_TAG,
                    "Got socket error %d (%s) while sending", 
                    err, strerror(err));
                break;
            }
        }
        
        if (totalTimeMs >= MAX_RETRY_TIME_MS)
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Wi-Fi subsystem took too long to become ready, dropping packet");
        }
        else if (tries > 1)
        {
            ESP_LOGW(CURRENT_LOG_TAG, "Needed %d tries to send a packet", tries++);
        }
    }
}

void FlexVitaTask::onEnableReportingMessage_(DVTask* origin, EnableReportingMessage* message)
{
    audioEnabled_ = true;
}

void FlexVitaTask::onDisableReportingMessage_(DVTask* origin, DisableReportingMessage* message)
{
    audioEnabled_ = false;
}

void FlexVitaTask::onRequestTxMessage_(DVTask* origin, audio::RequestTxMessage* message)
{
    isTransmitting_ = true;

    // Reset packet timing parameters so we can redetermine how quickly we need to be
    // sending packets.
    minPacketsRequired_ = MIN_VITA_PACKETS_TO_SEND;
    timeBeyondExpectedUs_ = 0;
    lastVitaGenerationTime_ = esp_timer_get_time();
    packetWriteTimer_.stop();
    packetWriteTimer_.start();
}

void FlexVitaTask::onRequestRxMessage_(DVTask* origin, audio::TransmitCompleteMessage* message)
{
    isTransmitting_ = false;

    // Reset packet timing parameters so we can redetermine how quickly we need to be
    // sending packets.
    minPacketsRequired_ = MIN_VITA_PACKETS_TO_SEND;
    timeBeyondExpectedUs_ = 0;
    lastVitaGenerationTime_ = esp_timer_get_time();
    packetWriteTimer_.stop();
    packetWriteTimer_.start();
}
    
}

}

}