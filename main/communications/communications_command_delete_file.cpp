#include "communications.h"
#include "communications_command_delete_file.h"
#include "fat32.h"

#define DEL_FILE_TAG "DeleteFileTask"

CComsCommandDeleteFile::CComsCommandDeleteFile(uint8_t cmd, uint32_t timeout) : CComsCommand(cmd, timeout)
{

}

CComsCommandDeleteFile::~CComsCommandDeleteFile()
{
    CComsCommand::~CComsCommand();
}

COMReturn CComsCommandDeleteFile::start(CPacket* packet)
{
    memcpy(fileName, (char*)packet->data(), packet->size());
    fileName[packet->size()] = 0;

    packet->clear();

    ESP_LOGD(DEL_FILE_TAG, "start(): complete");

    return CComsCommand::start(packet);
}

COMReturn CComsCommandDeleteFile::end(CPacket* packet)
{
    packet->clear();

    return CComsCommand::end(packet);
}

COMReturn CComsCommandDeleteFile::idle(CPacket* packet)
{
    if (Fat32.deleteFile(fileName) != FAT_RET_OK) {
        ESP_LOGI(DEL_FILE_TAG, "idle(): File failed to delete [%s]", fileName);

        uint8_t ret = 0xFF;
        packet->copy(&ret, 1);

        return COM_ERROR;
    }

    ESP_LOGI(DEL_FILE_TAG, "idle(): File deleted [%s]", fileName);

    uint8_t ret = 0x01;
    packet->copy(&ret, 1);

    return COM_COMPLETE;
}

COMReturn CComsCommandDeleteFile::receive(CPacket* packet)
{
    return CComsCommand::receive(packet);
}