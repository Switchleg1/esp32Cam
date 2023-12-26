#include <stdio.h>
#include <string.h>
#include "packet.h"


CPacket::CPacket()
{
    freeOnDelete    = false;
    dataPosition    = 0;
    dataPointer     = NULL;
    dataSize        = 0;
}

CPacket::CPacket(uint16_t len)
{
    freeOnDelete    = true;
    dataPosition    = 0;
    dataPointer     = (uint8_t*)malloc(len);
    if (dataPointer) {
        dataSize = len;
    }
}

CPacket::CPacket(uint8_t* src, uint16_t len, bool copyData)
{
    freeOnDelete    = false;
    dataPosition    = 0;
    dataPointer     = NULL;

    if (copyData) copy(src, len);
    else equal(src, len);
}

CPacket::CPacket(uint8_t* header, uint16_t headerLen, uint8_t* src, uint16_t srcLen)
{
    freeOnDelete    = true;
    dataPosition    = 0;
    dataPointer     = (uint8_t*)malloc(headerLen + srcLen);
    if (dataPointer) {
        memcpy(dataPointer, header, headerLen);
        memcpy(dataPointer + headerLen, src, srcLen);
        dataSize = headerLen + srcLen;
    }
}

CPacket::~CPacket()
{
    clear();
}

bool CPacket::valid()
{
    if (dataPointer && size()) {
        return true;
    }

    return false;
}

void CPacket::clear()
{
    if (freeOnDelete) {
        if (dataPointer) {
            free(dataPointer);
        }
    }

    freeOnDelete    = false;
    dataPointer     = NULL;
    dataSize        = 0;
    dataPosition    = 0;
}

int CPacket::copy(uint8_t* src, uint16_t len)
{
    clear();

    freeOnDelete    = true;
    dataPointer     = (uint8_t*)malloc(len);
    if (dataPointer) {
        memcpy(dataPointer, src, len);
        dataSize = len;
        return PKT_RET_OK;
    }

    return PKT_RET_MALLOC_ERROR;
}

int CPacket::equal(uint8_t* src, uint16_t len)
{
    clear();

    freeOnDelete    = false;
    dataPointer     = src;
    if (dataPointer) {
        dataSize = len;
        return PKT_RET_OK;
    }

    return PKT_RET_MALLOC_ERROR;
}

int CPacket::take(uint8_t* src, uint16_t len)
{
    clear();

    freeOnDelete    = true;
    dataPointer     = src;
    if (dataPointer) {
        dataSize = len;
        return PKT_RET_OK;
    }

    return PKT_RET_MALLOC_ERROR;
}

int CPacket::add(uint8_t* src, uint16_t len, uint16_t index, bool relativePosition)
{
    if (relativePosition) {
        index += dataPosition;
    }

    if (!dataPointer || !dataSize) {
        index = 0;
    } else if (index >= dataSize) {
        index = dataSize - 1;
    }

    uint16_t currentPosition    = dataPosition;
    uint16_t newSize            = dataSize + len;
    uint8_t* newBuffer          = (uint8_t*)malloc(newSize);
    if (newBuffer) {
        if (dataPointer) {
            memcpy(newBuffer, dataPointer, index);
        }
        memcpy(newBuffer + index, src, len);
        if (dataPointer) {
            memcpy(newBuffer + index + len, dataPointer + index, dataSize - index);
        }
        clear();
        freeOnDelete    = true;
        dataPointer     = newBuffer;
        dataSize        = newSize;
        dataPosition    = currentPosition;

        if (index < dataPosition) {
            dataPosition += len;
        }

        return PKT_RET_OK;
    }

    return PKT_RET_MALLOC_ERROR;
}

int CPacket::addSpace(uint16_t len)
{
    uint16_t currentPosition    = dataPosition;
    uint16_t newSize            = dataSize + len;
    uint8_t* newBuffer          = (uint8_t*)malloc(newSize);
    if (newBuffer) {
        if (dataPointer) {
            memcpy(newBuffer, dataPointer, dataSize);
        }
        clear();
        freeOnDelete    = true;
        dataPointer     = newBuffer;
        dataSize        = newSize;
        dataPosition    = currentPosition;

        return PKT_RET_OK;
    }

    return PKT_RET_MALLOC_ERROR;
}

int CPacket::remove(uint16_t len, uint16_t index, bool relativePosition)
{
    if (relativePosition) {
        index += dataPosition;
    }

    if (!dataPointer || index >= dataSize) {
        return PKT_RET_OK;
    }

    if (index + len > dataSize) {
        len = dataSize - index;
    }

    uint16_t currentPosition    = dataPosition;
    uint16_t newSize            = dataSize - len;
    uint8_t* newBuffer          = (uint8_t*)malloc(newSize);
    if (newBuffer) {
        memcpy(newBuffer, dataPointer, index);
        memcpy(newBuffer + index, dataPointer + index + len, dataSize - index - len);
        clear();
        freeOnDelete    = true;
        dataPointer     = newBuffer;
        dataSize        = newSize;
        dataPosition    = currentPosition;

        if (index < dataPosition) {
            if (index + len > dataPosition) {
                dataPosition = index;
            }
            else {
                dataPosition -= len;
            }
        }

        if (dataPosition > dataSize) {
            dataPosition = dataSize;
        }

        return PKT_RET_OK;
    }

    return PKT_RET_MALLOC_ERROR;
}

int CPacket::removeSpace(uint16_t len)
{
    if (len > dataSize) {
        len = dataSize;
    }

    dataSize -= len;

    if (dataPosition > dataSize) {
        dataPosition = dataSize;
    }

    return len;
}

int CPacket::forward(uint16_t len)
{
    if (!dataSize) {
        return 0;
    }

    dataPosition += len;
    if (dataPosition > dataSize) {
        uint16_t overSize   = dataPosition - dataSize;
        dataPosition        = dataSize - 1;
        return len - overSize;
    }

    return len;
}

int CPacket::back(uint16_t len)
{
    if (len > dataPosition) {
        uint16_t amount = dataPosition;
        dataPosition    = 0;

        return amount;
    }

    dataPosition -= len;

    return len;
}

void CPacket::rewind()
{
    dataPosition = 0;
}

int CPacket::position()
{
    return dataPosition;
}

uint8_t* CPacket::data()
{
    return dataPointer + dataPosition;
}

uint16_t CPacket::size()
{
    return dataSize - dataPosition;
}