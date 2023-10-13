#ifndef PACKET_H
#define PACKET_H

#include "globals.h"

//packet command flags
#define PKT_COMMAND_FLAG_SPLIT_PK       1

#define PKT_RET_OK                      0
#define PKT_RET_MALLOC_ERROR            1
#define PKT_RET_INVALID_BUFFER          2
#define PKT_RET_INCOMPLETE              3
#define PKT_RET_COMPLETE                4

class CPacket {
public:
    CPacket();
    CPacket(uint16_t len);
    CPacket(uint8_t* src, uint16_t len, bool copyData = true);
    CPacket(uint8_t* header, uint16_t headerLen, uint8_t* src, uint16_t srcLen);
    ~CPacket();

    bool    valid();
    void    clear();
    int     copy(uint8_t* src, uint16_t len);
    int     equal(uint8_t* src, uint16_t len);
    int     add(uint8_t* src, uint16_t len, uint16_t index);

    uint8_t* data;
    uint16_t size;

private:
    bool    freeOnDelete;
};

#endif