#include "communications.h"
#include "communications_command_camera.h"
#include "fat32.h"

#define CAM_TAG "SendFrameCommand"

CComsCommandCamera* pCameraCurrent = NULL;

CComsCommandCamera::CComsCommandCamera(uint8_t cmd, uint32_t timeout) : CComsCommand(cmd, timeout)
{
    pCameraCurrent  = this;
    currentFrame    = NULL;

    Camera.setOnFrameCallback(onFrame);
}

CComsCommandCamera::~CComsCommandCamera()
{
    CComsCommand::~CComsCommand();

    clearFrame();
}

COMReturn CComsCommandCamera::start(CPacket* packet)
{
    framePacketNumber   = 0;
    frameComplete       = false;

    packet->clear();

    return CComsCommand::start(packet);
}

COMReturn CComsCommandCamera::end(CPacket* packet)
{
    packet->clear();

    clearFrame();

    if (frameComplete) {
        uint8_t response = 0xFF;
        packet->copy(&response, 1);
    }

    return CComsCommand::end(packet);
}

COMReturn CComsCommandCamera::idle(CPacket* packet)
{
    return CComsCommand::idle(packet);
}

COMReturn CComsCommandCamera::receive(CPacket* packet)
{
    uint8_t command = packet->data()[0];
    packet->forward(1);

    switch (command) {
    case 0x10:
        return sendFrame(packet);
        break;

    case 0x11:
        return resendFrame(packet);
        break;
    }

    packet->clear();

    return COM_OK;
}

COMReturn CComsCommandCamera::sendFrame(CPacket* packet)
{
    packet->clear();

    if (!currentFrame) {
        ESP_LOGW(CAM_TAG, "sendFrame: frame not available");
        return COM_WAIT;
    }

    ESP_LOGD(CAM_TAG, "sendFrame: Reading packet [%u]", framePacketNumber);

    uint32_t packetPosition = framePacketNumber * COMS_DEFAULT_PKT_SIZE;
    int32_t dataLeft = currentFrame->len - packetPosition;
    if (dataLeft < 1) dataLeft = 0;
    uint32_t packetSize = COMS_DEFAULT_PKT_SIZE < dataLeft ? COMS_DEFAULT_PKT_SIZE : dataLeft;
    uint8_t* dataCopy = (uint8_t*)malloc(packetSize + sizeof(uint16_t));
    *((uint16_t*)dataCopy) = framePacketNumber;
    memcpy(dataCopy + sizeof(uint16_t), currentFrame->buf + packetPosition, packetSize);
    packet->take(dataCopy, packetSize + sizeof(uint16_t));

    if (packetSize < COMS_DEFAULT_PKT_SIZE) {
        frameComplete = true;
        ESP_LOGI(CAM_TAG, "sendFrame: Transfer complete [%u]", currentFrame->len);

        return COM_COMPLETE;
    }

    ESP_LOGD(CAM_TAG, "sendFrame: Packet sent [%lu]", packetSize);

    framePacketNumber++;

    return CComsCommand::receive(packet);
}

COMReturn CComsCommandCamera::resendFrame(CPacket* packet)
{
    if (packet->size() == sizeof(uint16_t)) {
        framePacketNumber = *((uint16_t*)packet->data());

        ESP_LOGW(CAM_TAG, "resendFrame: Resending packet [%u]", framePacketNumber);
        sendFrame(packet);

        return COM_OK;
    }

    ESP_LOGE(CAM_TAG, "resendFrame: Invalid request size [%u]", packet->size());

    return COM_ERROR;
}

void CComsCommandCamera::clearFrame()
{
    if (currentFrame) {
        esp_camera_fb_return(currentFrame);
        currentFrame = NULL;
    }
}

bool CComsCommandCamera::onFrame(camera_fb_t* frame)
{
    if (pCameraCurrent->started()) {
        if (pCameraCurrent->currentFrame == NULL) {
            ESP_LOGD(CAM_TAG, "onFrame: grabbed frame");

            pCameraCurrent->clearFrame();
            pCameraCurrent->currentFrame = frame;

            return true;
        }
    }
    else {
        pCameraCurrent->clearFrame();
    }

    return false;
}