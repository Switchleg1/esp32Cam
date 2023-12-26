#include "communications.h"
#include "communications_command_send_file.h"
#include "fat32.h"

#define SEND_FILE_TAG "SendFileTask"

CComsCommandSendFile::CComsCommandSendFile(uint8_t cmd, uint32_t timeout) : CComsCommand(cmd, timeout)
{
    readFile            = NULL;
}

CComsCommandSendFile::~CComsCommandSendFile()
{
    CComsCommand::~CComsCommand();

    closeFile();
}

COMReturn CComsCommandSendFile::start(CPacket* packet)
{
    fileComplete        = false;
    filePacketNumber    = 0;

    memcpy(fileName, (char*)packet->data(), packet->size());
    fileName[packet->size()] = 0;

    packet->clear();

    closeFile();

    readFile = fopen(fileName, "r");
    if (!readFile) {
        ESP_LOGE(SEND_FILE_TAG, "openFile(): unable to open");

        return COM_ERROR;
    }

    ESP_LOGI(SEND_FILE_TAG, "openFile(): opened file [%s]", fileName);

    return CComsCommand::start(packet);
}

COMReturn CComsCommandSendFile::end(CPacket* packet)
{
    packet->clear();

    closeFile();

    if (fileComplete) {
        uint8_t response = 0xFF;
        packet->copy(&response, 1);
    }

    return CComsCommand::end(packet);
}

COMReturn CComsCommandSendFile::idle(CPacket* packet)
{
    return CComsCommand::idle(packet);
}

COMReturn CComsCommandSendFile::receive(CPacket* packet)
{
    uint8_t command = packet->data()[0];
    packet->forward(1);

    switch (command) {
    case 0x10:
        return sendFile(packet);
        break;

    case 0x11:
        return resendFile(packet);
        break;
    }

    packet->clear();

    return COM_ERROR_INVALID;
}

COMReturn CComsCommandSendFile::sendFile(CPacket* packet)
{
    if (!readFile) {
        ESP_LOGW(SEND_FILE_TAG, "sendFile: File not open");

        return COM_ERROR;
    }

    ESP_LOGI(SEND_FILE_TAG, "sendFile: Reading packet [%u]", filePacketNumber);
    uint8_t* dataRead = (uint8_t*)malloc(COMS_DEFAULT_PKT_SIZE + sizeof(uint16_t));
    if (!dataRead) {
        ESP_LOGE(SEND_FILE_TAG, "sendFile: Unable to alloc memory to read file");

        return COM_ERROR_MALLOC;
    }
    *((uint16_t*)dataRead) = filePacketNumber;
    int16_t sizeRead = fread(dataRead + sizeof(uint16_t), 1, COMS_DEFAULT_PKT_SIZE, readFile);
    if (sizeRead < 0) {
        ESP_LOGE(SEND_FILE_TAG, "sendFile: Unable to read file");
        free(dataRead);

        return COM_ERROR_NOT_FOUND;
    }
    packet->take(dataRead, sizeRead);

    if (packet->size() < COMS_DEFAULT_PKT_SIZE) {
        fileComplete = true;
        ESP_LOGW(SEND_FILE_TAG, "sendFile: File transfer complete");

        return COM_COMPLETE;
    }

    filePacketNumber++;

    return CComsCommand::receive(packet);
}

COMReturn CComsCommandSendFile::resendFile(CPacket* packet)
{
    if (packet->size() == sizeof(uint16_t)) {
        filePacketNumber = *((uint16_t*)packet->data());

        ESP_LOGW(SEND_FILE_TAG, "resendFile: Resending packet [%u]", filePacketNumber);
        fseek(readFile, COMS_DEFAULT_PKT_SIZE * filePacketNumber, SEEK_SET);
        sendFile(packet);

        return COM_OK;
    }

    ESP_LOGE(SEND_FILE_TAG, "resendFile: Invalid request size [%u]", packet->size());

    return COM_ERROR;
}

void CComsCommandSendFile::closeFile()
{
    if (readFile) {
        fclose(readFile);
        readFile = NULL;

        ESP_LOGI(SEND_FILE_TAG, "closeFile(): File closed [%s]", fileName);
    }
}