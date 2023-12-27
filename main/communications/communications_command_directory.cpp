#include "communications.h"
#include "communications_command_directory.h"
#include "fat32.h"

#define DIR_TAG "SendDirectoryTask"

CComsCommandDirectory::CComsCommandDirectory(uint8_t cmd, uint32_t timeout) : CComsCommand(cmd, timeout)
{
}

CComsCommandDirectory::~CComsCommandDirectory()
{
    CComsCommand::~CComsCommand();
}

COMReturn CComsCommandDirectory::start(CPacket* packet)
{
    memcpy(fileName, (char*)packet->data(), packet->size());
    fileName[packet->size()] = 0;

    packet->clear();

    ESP_LOGI(DIR_TAG, "start(): complete [%s]", fileName);

    return CComsCommand::start(packet);
}

COMReturn CComsCommandDirectory::end(CPacket* packet)
{
    packet->clear();

    return CComsCommand::end(packet);
}

COMReturn CComsCommandDirectory::idle(CPacket* packet)
{
    uint16_t size = 1024;
    uint8_t* data = (uint8_t*)malloc(size);

    Fat32.listDir(fileName, 1, &data, &size);

    packet->take(data, size);

    ESP_LOGI(DIR_TAG, "idle(): complete [%u]", packet->size());

    return COM_COMPLETE;
}

COMReturn CComsCommandDirectory::receive(CPacket* packet)
{
    packet->clear();

    return COM_OK;
}