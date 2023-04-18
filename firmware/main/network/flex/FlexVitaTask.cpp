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

#include "codec2_fifo.h"
#include "codec2_fdmdv.h"

#define MAX_VITA_SAMPLES (42)
#define MAX_VITA_SAMPLES_TO_SEND (MAX_VITA_SAMPLES * FDMDV_OS_24) /* XXX: SmartSDR crashes if I try to send all 180. */

#define CURRENT_LOG_TAG "FlexVitaTask"

namespace ezdv
{

namespace network
{
    
namespace flex
{
    
static float tx_scale_factor = exp(6.0f/20.0f * log(10.0f));

FlexVitaTask::FlexVitaTask()
    : DVTask("FlexVitaTask", 11 /* TBD */, 8192, tskNO_AFFINITY, 1024, pdMS_TO_TICKS(10))
    , audio::AudioInput(2, 2)
    , packetReadTimer_(this, std::bind(&FlexVitaTask::readPendingPackets_, this), 5000)
    , socket_(-1)
    , rxStreamId_(0)
    , txStreamId_(0)
    , audioSeqNum_(0)
    , currentTime_(0)
    , timeFracSeq_(0)
{
    registerMessageHandler(this, &FlexVitaTask::onFlexConnectRadioMessage_);
    registerMessageHandler(this, &FlexVitaTask::onReceiveVitaMessage_);
    registerMessageHandler(this, &FlexVitaTask::onSendVitaMessage_);
    registerMessageHandler(this, &FlexVitaTask::onWirelessNetworkStatusMessage_);
    
    downsamplerInBuf_ = (float*)heap_caps_malloc((MAX_VITA_SAMPLES * FDMDV_OS_24 + FDMDV_OS_TAPS_24K) * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
    assert(downsamplerInBuf_ != nullptr);
    downsamplerOutBuf_ = (short*)heap_caps_malloc(MAX_VITA_SAMPLES * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
    assert(downsamplerOutBuf_ != nullptr);
    upsamplerInBuf_ = (short*)heap_caps_malloc((MAX_VITA_SAMPLES + FDMDV_OS_TAPS_24_8K) * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
    assert(upsamplerInBuf_ != nullptr);
    upsamplerOutBuf_ = (float*)heap_caps_malloc((MAX_VITA_SAMPLES * FDMDV_OS_24) * sizeof(float), MALLOC_CAP_SPIRAM | MALLOC_CAP_32BIT);
    assert(upsamplerOutBuf_ != nullptr);
}

FlexVitaTask::~FlexVitaTask()
{
    disconnect_();
    
    heap_caps_free(downsamplerInBuf_);
    heap_caps_free(upsamplerInBuf_);
    heap_caps_free(downsamplerOutBuf_);
    heap_caps_free(upsamplerOutBuf_);
}

void FlexVitaTask::onTaskStart_()
{
    // empty, wait for Wi-Fi to come up
}

void FlexVitaTask::onTaskWake_()
{
    // empty, wait for Wi-Fi to come up
}

void FlexVitaTask::onTaskSleep_()
{
    ESP_LOGI(CURRENT_LOG_TAG, "Sleeping task");
    disconnect_();
}

void FlexVitaTask::onTaskTick_()
{
    if (socket_ <= 0)
    {
        // Skip tick if we don't have a valid connection yet.
        return;
    }
    
    // Generate packets for both RX and TX.
    if (rxStreamId_)
    {
        generateVitaPackets_(audio::AudioInput::USER_CHANNEL, rxStreamId_);
    }
    
    if (txStreamId_)
    {
        generateVitaPackets_(audio::AudioInput::RADIO_CHANNEL, txStreamId_);
    }
}

void FlexVitaTask::generateVitaPackets_(audio::AudioInput::ChannelLabel channel, uint32_t streamId)
{
    auto fifo = getAudioInput(channel);
    
    while(codec2_fifo_read(fifo, &upsamplerInBuf_[FDMDV_OS_TAPS_24_8K], MAX_VITA_SAMPLES) == 0)
    {
        if (streamId == txStreamId_)
        {
            for (int i = 0; i < MAX_VITA_SAMPLES; i++)
            {
                upsamplerInBuf_[FDMDV_OS_TAPS_24_8K + i] *= tx_scale_factor;
            }
        }
        
        vita_packet* packet = new vita_packet;
        assert(packet != nullptr);
        
        // Upsample to 24K floats.
        fdmdv_8_to_24(upsamplerOutBuf_, &upsamplerInBuf_[FDMDV_OS_TAPS_24_8K], MAX_VITA_SAMPLES);
  
        // Fil in packet with data
        packet->packet_type = VITA_PACKET_TYPE_IF_DATA_WITH_STREAM_ID;
        packet->stream_id = streamId;
        packet->class_id = AUDIO_CLASS_ID;
        packet->timestamp_type = audioSeqNum_++;
    
        for (unsigned int i = 0, j = 0; i < MAX_VITA_SAMPLES_TO_SEND; ++i, j += 2)
        {
            packet->if_samples[j] = packet->if_samples[j + 1] = htonl(*(uint32_t*)&upsamplerOutBuf_[i]);
        }
        
        size_t packet_len = VITA_PACKET_HEADER_SIZE + MAX_VITA_SAMPLES_TO_SEND * 2 * sizeof(float);

        //  XXX Lots of magic numbers here!
        packet->timestamp_type = 0x50u | (packet->timestamp_type & 0x0Fu);
        assert(packet_len % 4 == 0);
        packet->length = htons(packet_len >> 2); // Length is in 32-bit words, note there are two channels

        packet->timestamp_int = time(NULL);
        if (packet->timestamp_int != currentTime_)
        {
            timeFracSeq_ = 0;
        }
        
        packet->timestamp_frac = timeFracSeq_++;
        currentTime_ = packet->timestamp_int;

        SendVitaMessage message(packet, packet_len);
        post(&message);
    }
}

void FlexVitaTask::disconnect_()
{
    if (socket_ > 0)
    {
        packetReadTimer_.stop();
        close(socket_);
        socket_ = -1;
        
        rxStreamId_ = 0;
        txStreamId_ = 0;
        audioSeqNum_ = 0;
        currentTime_ = 0;
        timeFracSeq_ = 0;
        
        inputVector_.clear();
    }
}

void FlexVitaTask::readPendingPackets_()
{
    fd_set readSet;
    struct timeval tv = {0, 0};
    
    FD_ZERO(&readSet);
    FD_SET(socket_, &readSet);
    
    // Process if there are pending datagrams in the buffer
    while (select(socket_ + 1, &readSet, nullptr, nullptr, &tv) > 0)
    {
        vita_packet* packet = new vita_packet;
        assert(packet != nullptr);
        
        auto rv = recv(socket_, (char*)packet, sizeof(vita_packet), 0);
        if (rv > 0)
        {
            // Queue up packet for future processing.
            ReceiveVitaMessage message(packet, rv);
            post(&message);
        }
        else
        {
            delete packet;
        }
        
        // Reinitialize the read set for the next pass.
        FD_ZERO(&readSet);
        FD_SET(socket_, &readSet);
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

            for (unsigned int i = 0; i < half_num_samples; ++i)
            {
                uint32_t temp = ntohl(packet->if_samples[i << 1]);
                inputVector_.push_back(*(float*)&temp);
            }
            
            //ESP_LOGI(CURRENT_LOG_TAG, "Received %d samples from radio", half_num_samples);
            
            while (inputVector_.size() >= MAX_VITA_SAMPLES * FDMDV_OS_24)
            {
                for (int i = 0; i < MAX_VITA_SAMPLES * FDMDV_OS_24; i++)
                {
                    downsamplerInBuf_[FDMDV_OS_TAPS_24K + i] = inputVector_.front();
                    inputVector_.pop_front();
                }
            
                fdmdv_24_to_8(downsamplerOutBuf_, &downsamplerInBuf_[FDMDV_OS_TAPS_24K], MAX_VITA_SAMPLES);
            
                // Queue on respective FIFO.
                auto fifo = getAudioOutput(channel);
                if (fifo != nullptr)
                {
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
    delete message->packet;
}

void FlexVitaTask::onSendVitaMessage_(DVTask* origin, SendVitaMessage* message)
{
    auto packet = message->packet;
    assert(packet != nullptr);

    if (socket_ > 0)
    {
        int rv = sendto(socket_, (char*)packet, message->length, 0, (struct sockaddr*)&radioAddress_, sizeof(radioAddress_));
        int tries = 0;
        while (rv == -1 && tries < 100)
        {
            auto err = errno;
            if (err == ENOMEM)
            {
                // Wait a bit and try again; the Wi-Fi subsystem isn't ready yet.
                vTaskDelay(1);
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
        
        if (tries >= 100)
        {
            ESP_LOGE(CURRENT_LOG_TAG, "Wi-Fi subsystem took too long to become ready, dropping packet");
        }
        else if (tries > 0)
        {
            ESP_LOGW(CURRENT_LOG_TAG, "Needed %d tries to send a packet", tries++);
        }

        // Read any packets that are available from the radio
        readPendingPackets_();
    }
    
    delete packet;
}

void FlexVitaTask::onWirelessNetworkStatusMessage_(DVTask* origin, WirelessNetworkStatusMessage* message)
{
    if (message->state)
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
    
        ESP_LOGI(CURRENT_LOG_TAG, "Connected to radio successfully");
        fcntl (socket_, F_SETFL , O_NONBLOCK);
    
        packetReadTimer_.start();
    }
    else
    {
        disconnect_();
    }
}
    
}

}

}