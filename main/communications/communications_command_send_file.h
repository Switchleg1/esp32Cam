#ifndef COMMUNICATIONS_COMMAND_SEND_FILE_H
#define COMMUNICATIONS_COMMAND_SEND_FILE_H

#include "communications_globals.h"
#include "communications_command.h"

class CComsCommandSendFile : public CComsCommand {
public:
    CComsCommandSendFile(uint8_t cmd, uint32_t timeout);
    ~CComsCommandSendFile();

    COMReturn	start(CPacket* packet);
    COMReturn	end(CPacket* packet);
    COMReturn	idle(CPacket* packet);
    COMReturn	receive(CPacket* packet);

private:
    COMReturn   sendFile(CPacket* packet);
    COMReturn   resendFile(CPacket* packet);
    void        closeFile();

    char        fileName[512];
    bool        fileComplete;
    uint16_t    filePacketNumber;
    FILE*       readFile;
};

#endif