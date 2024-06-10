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

#include <string>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "PskReporterTask.h"
#include "esp_app_desc.h"

#define PSK_REPORTER_HOSTNAME "report.pskreporter.info"
#define PSK_REPORTER_PORT "4739" /* or 14739 for testing/debugging */
#define PSK_REPORTER_MODE "FREEDV"

// We should be sending every 5 min, but as an embedded device
// we can lose internet at any time, so a shorter interval is fine
// and unlikely to cause problems with the server.
#define PSK_REPORTER_SEND_INTERVAL_MS (60000) 

#define CURRENT_LOG_TAG "PskReporter"

// RX record:
/* For receiverCallsign, receiverLocator, decodingSoftware use */

static const unsigned char rxFormatHeader[] = {
    0x00, 0x03, 0x00, 0x24, 0x99, 0x92, 0x00, 0x03, 0x00, 0x00, 
    0x80, 0x02, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
    0x80, 0x04, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F, 
    0x80, 0x08, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F, 
    0x00, 0x00 
};

// TX record:
/* For senderCallsign, frequency (5 bytes--needed for 10+GHz), sNR (1 byte), mode, informationSource (1 byte), flowStartSeconds use */

static const unsigned char txFormatHeader[] = {
    0x00, 0x02, 0x00, 0x34, 0x99, 0x93, 0x00, 0x06,
    0x80, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
    0x80, 0x05, 0x00, 0x05, 0x00, 0x00, 0x76, 0x8F,
    0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 0x76, 0x8F,
    0x80, 0x0A, 0xFF, 0xFF, 0x00, 0x00, 0x76, 0x8F,
    0x80, 0x0B, 0x00, 0x01, 0x00, 0x00, 0x76, 0x8F,
    0x00, 0x96, 0x00, 0x04
};

namespace ezdv
{

namespace network
{

PskReporterTask::PskReporterTask()
    : ezdv::task::DVTask("PskReporterTask", 1, 4096, tskNO_AFFINITY, 128)
    , udpSendTimer_(this, this, &PskReporterTask::sendPskReporterRecords_, MS_TO_US(PSK_REPORTER_SEND_INTERVAL_MS), "PskReporterReconn")
    , reportingEnabled_(false)
    , callsign_("")
    , gridSquare_("UN00KN") // TBD: should come from flash
    , frequencyHz_(0)
    , forceReporting_(false)
    , reportingRefCount_(0)
    , currentSequenceNumber_(0)
{
    registerMessageHandler(this, &PskReporterTask::onReportingSettingsMessage_);
    registerMessageHandler(this, &PskReporterTask::onEnableReportingMessage_);
    registerMessageHandler(this, &PskReporterTask::onDisableReportingMessage_);
    registerMessageHandler(this, &PskReporterTask::onFreeDVCallsignReceivedMessage_);
    registerMessageHandler(this, &PskReporterTask::onReportFrequencyChangeMessage_);

    srand(time(0));
    randomIdentifier_ = rand();

    auto espVersionStruct = esp_app_get_description();
    decodingSoftware_ = "ezDV " + std::string(espVersionStruct->version);
}

PskReporterTask::~PskReporterTask()
{
    if (reportingEnabled_)
    {
        stopConnection_();
    }
}

void PskReporterTask::onTaskStart_()
{
    // Request current reporting settings
    storage::RequestReportingSettingsMessage reportingRequest;
    publish(&reportingRequest);

    // Request current FreeDV mode
    audio::RequestGetFreeDVModeMessage modeRequest;
    publish(&modeRequest);
}

void PskReporterTask::onTaskSleep_()
{
    if (reportingEnabled_)
    {
        stopConnection_();
    }
}

void PskReporterTask::onReportingSettingsMessage_(DVTask* origin, storage::ReportingSettingsMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Got reporting settings update");
    
    bool callsignChanged = callsign_ != message->callsign || gridSquare_ != message->gridSquare;
    bool forcedReportingChanged = forceReporting_ != message->forceReporting;
    
    callsign_ = message->callsign;
    gridSquare_ = message->gridSquare;
    forceReporting_ = message->forceReporting;
    
    // Disconnect and reconnect if there were any changes
    if (callsignChanged)
    {
        if (reportingEnabled_)
        {
            stopConnection_();
        }
        
        if (reportingRefCount_ > 0 && callsign_.length() > 0 && gridSquare_.length() > 0)
        {
            startConnection_();
        }
    }
    
    // If forced reporting has changed, trigger disconnect or connect
    // as appropriate.
    if (forcedReportingChanged)
    {
        if (forceReporting_)
        {
            ESP_LOGI(CURRENT_LOG_TAG, "Forcing reporting to go live");
            EnableReportingMessage request;
            post(&request);
        }
        else
        {
            ESP_LOGI(CURRENT_LOG_TAG, "Undoing forced reporting");
            DisableReportingMessage request;
            post(&request);
        }
    }
    
    // If reporting is forced, unconditionally send frequency update.
    if (forceReporting_)
    {
        ReportFrequencyChangeMessage request(message->freqHz);
        post(&request);
    }
}

void PskReporterTask::onEnableReportingMessage_(DVTask* origin, EnableReportingMessage* message)
{
    reportingRefCount_++;
    if (callsign_ != "" && gridSquare_ != "")
    {        
        ESP_LOGI(CURRENT_LOG_TAG, "Reporting enabled by radio driver, begin connection");
        if (reportingEnabled_)
        {
            stopConnection_();
        }
        startConnection_();
    }
}

void PskReporterTask::onDisableReportingMessage_(DVTask* origin, DisableReportingMessage* message)
{
    reportingRefCount_--;
    if (reportingEnabled_ && reportingRefCount_ == 0)
    {
        stopConnection_();
    }
}

void PskReporterTask::onFreeDVCallsignReceivedMessage_(DVTask* origin, audio::FreeDVReceivedCallsignMessage* message)
{
    if (reportingEnabled_)
    {
        ESP_LOGI(CURRENT_LOG_TAG, "Adding %s to callsign list", message->callsign);
        recordList_.push_back(SenderRecord(message->callsign, frequencyHz_, (int)message->snr));
    }
}

void PskReporterTask::onReportFrequencyChangeMessage_(DVTask* origin, ReportFrequencyChangeMessage* message)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Got frequency update: %" PRIu64, message->frequencyHz);
    frequencyHz_ = message->frequencyHz;
}

void PskReporterTask::stopConnection_()
{
    udpSendTimer_.stop();
    recordList_.clear();
    reportingEnabled_ = false;
}

void PskReporterTask::startConnection_()
{
    udpSendTimer_.start();
    currentSequenceNumber_ = 0;
    reportingEnabled_ = true;
}

void PskReporterTask::encodeReceiverRecord_(char* buf)
{
    // Encode RX record header.
    buf[0] = 0x99;
    buf[1] = 0x92;

    // Encode record size.
    char* fieldLoc = &buf[2];
    *((unsigned short*)fieldLoc) = htons(getRxDataSize_());

    // Encode RX callsign.
    fieldLoc += sizeof(unsigned short);
    *fieldLoc = (char)callsign_.size();
    memcpy(fieldLoc + 1, callsign_.c_str(), callsign_.size());

    // Encode RX locator.
    fieldLoc += 1 + callsign_.size();
    *fieldLoc = (char)gridSquare_.size();
    memcpy(fieldLoc + 1, gridSquare_.c_str(), gridSquare_.size());

    // Encode RX decoding software.
    fieldLoc += 1 + gridSquare_.size();
    *fieldLoc = (char)decodingSoftware_.size();
    memcpy(fieldLoc + 1, decodingSoftware_.c_str(), decodingSoftware_.size());
}

void PskReporterTask::encodeSenderRecords_(char* buf)
{
    if (recordList_.size() == 0) return;
    
    // Encode TX record header.
    buf[0] = 0x99;
    buf[1] = 0x93;

    // Encode record size.
    char* fieldLoc = &buf[2];
    *((unsigned short*)fieldLoc) = htons(getTxDataSize_());

    // Encode individual records.
    fieldLoc += sizeof(unsigned short);
    for(auto& rec : recordList_)
    {
        rec.encode(fieldLoc);
        fieldLoc += rec.recordSize();
    }
}

void PskReporterTask::sendPskReporterRecords_(DVTimer*)
{
    ESP_LOGI(CURRENT_LOG_TAG, "Sending currently queued records to server");

    // Header (2) + length (2) + time (4) + sequence # (4) + random identifier (4) +
    // RX format block + TX format block + RX data + TX data
    int dgSize = 16 + sizeof(rxFormatHeader) + sizeof(txFormatHeader) + getRxDataSize_() + getTxDataSize_();
    if (getTxDataSize_() == 0) dgSize -= sizeof(txFormatHeader);

    char* packet = new char[dgSize];
    memset(packet, 0, dgSize);

    // Encode packet header.
    packet[0] = 0x00;
    packet[1] = 0x0A;

    // Encode datagram size.
    char* fieldLoc = &packet[2];
    *((unsigned short*)fieldLoc) = htons(dgSize);

    // Encode send time.
    fieldLoc += sizeof(unsigned short);
    *((unsigned int*)fieldLoc) = htonl(time(0));

    // Encode sequence number.
    fieldLoc += sizeof(unsigned int);
    *((unsigned int*)fieldLoc) = htonl(currentSequenceNumber_++);

    // Encode random identifier.
    fieldLoc += sizeof(unsigned int);
    *((unsigned int*)fieldLoc) = htonl(randomIdentifier_);

    // Copy RX and TX format headers.
    fieldLoc += sizeof(unsigned int);
    memcpy(fieldLoc, rxFormatHeader, sizeof(rxFormatHeader));
    fieldLoc += sizeof(rxFormatHeader);

    if (getTxDataSize_() > 0)
    {
        memcpy(fieldLoc, txFormatHeader, sizeof(txFormatHeader));
        fieldLoc += sizeof(txFormatHeader);
    }

    // Encode receiver and sender records.
    encodeReceiverRecord_(fieldLoc);
    fieldLoc += getRxDataSize_();
    encodeSenderRecords_(fieldLoc);

    recordList_.clear();

    // Send to PSKReporter.    
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;
    hints.ai_flags = AI_ADDRCONFIG | AI_V4MAPPED | AI_NUMERICSERV;

    struct addrinfo* res = NULL;
    int err = getaddrinfo(PSK_REPORTER_HOSTNAME, PSK_REPORTER_PORT, &hints, &res);
    if (err != 0) 
    {
        ESP_LOGE(CURRENT_LOG_TAG, "cannot resolve %s (err=%d)", PSK_REPORTER_HOSTNAME, err);
        return;
    }

    int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if(fd < 0)
    {
        ESP_LOGE(CURRENT_LOG_TAG, "cannot open PSK Reporter socket (err=%d)\n", errno);
        freeaddrinfo(res);
        return;
    }

    if (sendto(fd, packet, dgSize, 0, res->ai_addr, res->ai_addrlen) < 0)
    {
        delete[] packet;
        ESP_LOGE(CURRENT_LOG_TAG, "cannot send message to PSK Reporter (err=%d)\n", errno);

        close(fd);
        freeaddrinfo(res);
        return;
    }

    delete[] packet;
    close(fd);

    freeaddrinfo(res);
}

int PskReporterTask::getRxDataSize_()
{
    int size = 4 + (1 + callsign_.size()) + (1 + gridSquare_.size()) + (1 + decodingSoftware_.size());
    if ((size % 4) > 0)
    {
        // Pad to aligned boundary.
        size += (4 - (size % 4));
    }
    return size;
}

int PskReporterTask::getTxDataSize_()
{
    if (recordList_.size() == 0)
    {
        return 0;
    }
    
    int size = 4;
    for (auto& item : recordList_)
    {
        size += item.recordSize();
    }
    if ((size % 4) > 0)
    {
        // Pad to aligned boundary.
        size += (4 - (size % 4));
    }
    return size;
}

PskReporterTask::SenderRecord::SenderRecord(std::string callsign, uint64_t frequency, char snr)
    : callsign(callsign)
    , frequency(frequency)
    , snr(snr)
{
    mode = "FREEDV";
    infoSource = 1;
    flowTimeSeconds = time(0);
} 

int PskReporterTask::SenderRecord::recordSize()
{
    return (1 + callsign.size()) + 5 + 1 + (1 + mode.size()) + 1 + 4;
}

void PskReporterTask::SenderRecord::encode(char* buf)
{    
    // Encode callsign
    char* fieldPtr = &buf[0];
    *fieldPtr = (char)callsign.size();
    memcpy(fieldPtr + 1, callsign.c_str(), callsign.size());
    
    // Encode frequency
    fieldPtr += 1 + callsign.size();
    *fieldPtr++ = ((frequency >> 32) & 0xff);;
    *fieldPtr++ = ((frequency >> 24) & 0xff);
    *fieldPtr++ = ((frequency >> 16) & 0xff);
    *fieldPtr++ = ((frequency >>  8) & 0xff);
    *fieldPtr++ = (frequency & 0xff);

    // Encode SNR
    *fieldPtr++ = snr;
    
    // Encode mode
    *fieldPtr = (char)mode.size();
    memcpy(fieldPtr + 1, mode.c_str(), mode.size());
    
    // Encode infoSource
    fieldPtr += 1 + mode.size();
    *fieldPtr++ = infoSource;
    
    // Encode flow start time
    *((unsigned int*)fieldPtr) = htonl(flowTimeSeconds);
}

}

}
