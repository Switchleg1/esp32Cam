#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include "globals.h"

#define RB_RET_OK               0
#define RB_RET_OVERFLOW         1
#define RB_RET_HEADER_NOT_FOUND 2
#define RB_RET_MALLOC_ERROR     3
#define RB_RET_PARTIAL_DATA     4
#define RB_RET_INVALID_PARAM    5

#define PKT_HEADER_ID           0xFF
#define PKT_LENGTH_POS          2
#define PKT_LENGTH_OFFSET       sizeof(packetHeader_t)

class CRingBuffer {
  public:
    CRingBuffer(uint16_t size);
    ~CRingBuffer();

    int               add(uint8_t* data, uint16_t size);
    int               parse(uint8_t** ptr, uint32_t* size);
    void              clear();
    
    void              take();
    void              release();
    
  private:
    int               bufferGet(uint8_t* data, uint16_t size);
    uint8_t           bufferCheckByte(uint16_t pos);
    uint16_t          bufferCheckWord(uint16_t pos);
    void              bufferClear();
    int               bufferCheckHeader();
  
    uint8_t*          bufferData;
    uint16_t          bufferDataSize;
    uint16_t          bufferLength;
    uint16_t          bufferPosition;
    bool              packetStarted;
    SemaphoreHandle_t mutex;
};

#endif
