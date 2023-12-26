#ifndef COMMUNICATIONS_COMMAND_DIRECTORY_H
#define COMMUNICATIONS_COMMAND_DIRECTORY_H

#include "communications_globals.h"

class CComsCommandDirectory : public CComsCommand {
public:
    CComsCommandDirectory(uint8_t cmd, uint32_t timeout);
    ~CComsCommandDirectory();

    COMReturn		start(CPacket* packet);
    COMReturn		end(CPacket* packet);
    COMReturn		idle(CPacket* packet);
    COMReturn		receive(CPacket* packet);

private:
    char            fileName[512];
};

#endif