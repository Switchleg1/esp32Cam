#include <stdio.h>
#include <string.h>
#include "packet.h"


CPacket::CPacket()
{
    freeOnDelete = false;
    data = NULL;
    size = 0;
}

CPacket::CPacket(uint16_t len)
{
    freeOnDelete = true;
    data = (uint8_t*)malloc(len);
    if (data) {
        size = len;
    }
}

CPacket::CPacket(uint8_t* src, uint16_t len, bool copyData)
{
    freeOnDelete = false;
    data = NULL;

    if (copyData) copy(src, len);
    else equal(src, len);
}

CPacket::CPacket(uint8_t* header, uint16_t headerLen, uint8_t* src, uint16_t srcLen)
{
    freeOnDelete = true;
    data = NULL;

    data = (uint8_t*)malloc(headerLen + srcLen);
    if (data) {
        memcpy(data, header, headerLen);
        memcpy(data + headerLen, src, srcLen);
        size = headerLen + srcLen;
    }
}

CPacket::~CPacket()
{
    clear();
}

bool CPacket::valid()
{
    if (data && size) {
        return true;
    }

    return false;
}

void CPacket::clear()
{
    if (freeOnDelete) {
        if (data) {
            free(data);
        }
    }

    freeOnDelete = false;
    data = NULL;
    size = 0;
}

int CPacket::copy(uint8_t* src, uint16_t len)
{
    clear();

    freeOnDelete = true;
    data = (uint8_t*)malloc(len);
    if (data) {
        memcpy(data, src, len);
        size = len;
        return PKT_RET_OK;
    }

    return PKT_RET_MALLOC_ERROR;
}

int CPacket::equal(uint8_t* src, uint16_t len)
{
    clear();

    freeOnDelete = false;
    data = src;
    if (data) {
        size = len;
        return PKT_RET_OK;
    }

    return PKT_RET_MALLOC_ERROR;
}

int CPacket::add(uint8_t* src, uint16_t len, uint16_t index)
{
    if (!data || !size) {
        index = 0;
    } else if (index >= size) {
        index = size - 1;
    }

    uint16_t newSize = size + len;
    uint8_t* newBuffer = (uint8_t*)malloc(newSize);
    if (newBuffer) {
        if (data) {
            memcpy(newBuffer, data, index);
        }
        memcpy(newBuffer + index, src, len);
        if (data) {
            memcpy(newBuffer + index + len, data + index, size - index);
        }
        clear();
        freeOnDelete = true;
        data = newBuffer;
        size = newSize;
    }
    return PKT_RET_OK;
}