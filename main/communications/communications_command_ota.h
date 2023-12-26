#ifndef COMMUNICATIONS_COMMAND_OTA_H
#define COMMUNICATIONS_COMMAND_OTA_H

#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "communications_globals.h"
#include "communications_command.h"

class CComsCommandOTA : public CComsCommand {
public:
    CComsCommandOTA(uint8_t cmd, uint32_t timeout);
    ~CComsCommandOTA();

    COMReturn			    start(CPacket* packet);
    COMReturn			    end(CPacket* packet);
    COMReturn			    idle(CPacket* packet);
    COMReturn			    receive(CPacket* packet);

private:
    COMReturn               receivePacket(CPacket* packet);

    uint16_t                packetNumber;
    esp_ota_handle_t        updateHandle;
    const esp_partition_t*  updatePartition;
    const esp_partition_t*  runningPartition;
    const esp_partition_t*  configuredPartition;
};

#endif