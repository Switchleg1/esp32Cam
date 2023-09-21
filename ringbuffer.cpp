#include "ringbuffer.h"

CRingBuffer::CRingBuffer(uint16_t size)
{
  clear();

  //create buffer
  bufferData = (uint8_t*)ps_malloc(size);
  if(bufferData) {
    bufferDataSize  = size;
  } else {
    bufferDataSize = 0;
  }

  mutex = xSemaphoreCreateMutex();
}

CRingBuffer::~CRingBuffer()
{
  if(bufferData) {
    free(bufferData);
  }
  
  if(mutex) {
    vSemaphoreDelete(mutex);
  }
}

int CRingBuffer::add(uint8_t* data, uint16_t size)
{
  bool      overflow  = false;
  uint16_t  tmp_pos   = 0;

  //check for over flow of the buffer
  if (size + bufferLength > bufferDataSize) {
    size = bufferDataSize - bufferLength;
    overflow = true;
    Serial.println("CRingBuffer::add: overflow");
  }

  //check current write position (buffer pos + length) and if its beyond buffer length, wrap
  uint16_t write_pos = (bufferPosition + bufferLength) % bufferDataSize;

  //if the size of the data extends beyond end of buffer, write partial
  if (write_pos + size > bufferDataSize) {
    uint16_t partial_size = bufferDataSize - write_pos;
    memcpy(&bufferData[write_pos], data, partial_size);
    size -= partial_size;
    bufferLength += partial_size;
    tmp_pos = partial_size;
    write_pos = 0;
  }

  //write the remaining data to buffer
  if (size) {
    memcpy(&bufferData[write_pos], &data[tmp_pos], size);
    bufferLength += size;
  }

  Serial.printf("CRingBuffer::add: added [%d] data size [%d] position [%d]\n", size, bufferLength, bufferPosition);

  return overflow;
}

int CRingBuffer::parse(uint8_t** ptr, uint32_t* size)
{
  if (!size || !ptr) {
    return RB_RET_INVALID_PARAM;
  }
  
  if (!bufferCheckHeader()) {
    return RB_RET_HEADER_NOT_FOUND;
  }

  //we found the packet header so we can safely say the packet has started
  packetStarted = true;

  //get packet length
  *size = bufferCheckWord(PKT_LENGTH_POS) + PKT_LENGTH_OFFSET;
  if (*size > bufferDataSize) {
    *size = bufferDataSize;
  }

  //if data is smaller than packet length abort
  if (bufferLength < *size) {
    return RB_RET_PARTIAL_DATA;
  }

  //allocate buffer
  *ptr = (uint8_t*)ps_malloc(*size);
  if (!*ptr) {
    Serial.printf("CRingBuffer::parse: malloc error size [%d]", *size);

    //clear current buffer on malloc error
    clear();
    
    return RB_RET_MALLOC_ERROR;
  }

  packetStarted = false;
  bufferGet(*ptr, *size);
  
  return RB_RET_OK;
}

void CRingBuffer::clear()
{
  packetStarted   = false;
  bufferLength    = 0;
  bufferPosition  = 0;
}

void CRingBuffer::take()
{
  xSemaphoreTake(mutex, portMAX_DELAY);
}

void CRingBuffer::release()
{
  xSemaphoreGive(mutex);
}

int CRingBuffer::bufferGet(uint8_t* data, uint16_t size)
{
  bool      overflow  = false;
  uint16_t  tmp_pos   = 0;

  //check for over flow of the buffer
  if (size > bufferLength) {
    size = bufferLength;
    overflow = true;
    Serial.println("CRingBuffer::bufferGet: overflow");
  }

  //check current to see if we will be reading beyond the end of the buffer, if so get partial data from end and wrap afterwards
  if (bufferPosition + size > bufferDataSize) {
    uint16_t partial_size = bufferDataSize - bufferPosition;
    memcpy(data, &bufferData[bufferPosition], partial_size);
    size -= partial_size;
    bufferLength -= partial_size;
    bufferPosition = 0;
    tmp_pos = partial_size;
  }

  //read the remaining data from buffer
  if (size) {
    memcpy(&data[tmp_pos], &bufferData[bufferPosition], size);
    bufferLength -= size;
    bufferPosition += size;
  }

  Serial.printf("CRingBuffer::bufferGet: removed [%d] data left [%d] position [%d]\n", size, bufferLength, bufferPosition);

  return overflow;
}

uint8_t CRingBuffer::bufferCheckByte(uint16_t pos)
{
  uint16_t tmp_pos = bufferPosition + pos;
  if (tmp_pos >= bufferDataSize) tmp_pos -= bufferDataSize;
  uint8_t tmp_data = bufferData[tmp_pos];

  return tmp_data;
}

uint16_t CRingBuffer::bufferCheckWord(uint16_t pos)
{
  uint16_t tmp_pos  = (bufferPosition + pos) % bufferDataSize;
  uint16_t tmp_pos1 = (tmp_pos + 1) % bufferDataSize;
  uint16_t tmp_data = (bufferData[tmp_pos1] << 8) + bufferData[tmp_pos];

  return tmp_data;
}

int CRingBuffer::bufferCheckHeader()
{
  if (bufferLength) {
    if (bufferCheckByte(0) == PKT_HEADER_ID) {
      if (bufferLength >= sizeof(packetHeader_t)) {

        //check command length, fail if oversized
        uint16_t packet_len = bufferCheckWord(PKT_LENGTH_POS) + sizeof(packetHeader_t);
        if (packet_len > bufferDataSize) {
          bufferClear();
          return false;
        }

        return true;
      }
    }
    else {
      Serial.printf("CRingBuffer::bufferCheckWord: removed [%d]\n", bufferLength);
      bufferClear();
      return false;
    }
  }

  return false;
}
