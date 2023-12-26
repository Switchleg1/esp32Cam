#ifndef COMMUNICATIONS_COMMAND_CAMERA_H
#define COMMUNICATIONS_COMMAND_CAMERA_H

#include "communications_globals.h"
#include "communications_command.h"
#include "camera.h"

class CComsCommandCamera : public CComsCommand {
public:
    CComsCommandCamera(uint8_t cmd, uint32_t timeout);
    ~CComsCommandCamera();

    COMReturn			start(CPacket* packet);
    COMReturn			end(CPacket* packet);
    COMReturn			idle(CPacket* packet);
    COMReturn			receive(CPacket* packet);

private:
    COMReturn           sendFrame(CPacket* packet);
    COMReturn           resendFrame(CPacket* packet);
    void                clearFrame();
    static bool         onFrame(camera_fb_t* frame);

    char                fileName[512];
    camera_fb_t*        currentFrame;
    uint16_t            framePacketNumber;
    bool                frameComplete;
};

#endif