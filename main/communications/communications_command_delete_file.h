#ifndef COMMUNICATIONS_COMMAND_DELETE_FILE_H
#define COMMUNICATIONS_COMMAND_DELETE_FILE_H

#include "communications_globals.h"
#include "communications_command.h"

class CComsCommandDeleteFile : public CComsCommand {
public:
    CComsCommandDeleteFile(uint8_t cmd, uint32_t timeout);
    ~CComsCommandDeleteFile();

    COMReturn	start(CPacket* packet);
    COMReturn	end(CPacket* packet);
    COMReturn	idle(CPacket* packet);
    COMReturn	receive(CPacket* packet);

private:
    COMReturn   deleteFile(CPacket* packet);

    char        fileName[512];
};

#endif