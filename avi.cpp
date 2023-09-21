#include "avi.h"

const uint8_t dcBuf[4]    = {0x30, 0x30, 0x64, 0x63}; // 00dc
const uint8_t wbBuf[4]    = {0x30, 0x31, 0x77, 0x62}; // 01wb
const uint8_t idx1Buf[4]  = {0x69, 0x64, 0x78, 0x31}; // idx1
const uint8_t zeroBuf[4]  = {0x00, 0x00, 0x00, 0x00}; // 0000


struct riffH {
  uint8_t   RIFF[4] = {0x52, 0x49, 0x46, 0x46};  //RIFF
  uint32_t  dwSize;
  uint8_t   AVI_[4] = {0x41, 0x56, 0x49, 0x20};  //AVI
};

struct hdrlH {
  uint8_t   LIST[4] = {0x4C, 0x49, 0x53, 0x54};  //LIST
  uint32_t  dwSize;
  uint8_t   hdrl[4] = {0x68, 0x64, 0x72, 0x6C};  //hdrl
};

struct avihH {
  uint8_t   avih[4]               = {0x61, 0x76, 0x69, 0x68};  //avih
  uint32_t  dwSize                = sizeof(avihH) - 8;
  uint32_t  dwMicroSecPerFrame;
  uint32_t  dwMaxBytesPerSec;
  uint32_t  dwPaddingGranularity;
  uint32_t  dwFlags;
  uint32_t  dwTotalFrames;
  uint32_t  dwInitialFrames;
  uint32_t  dwStreams;
  uint32_t  dwSuggestedBufferSize;
  uint32_t  dwWidth;
  uint32_t  dwHeight;
  uint32_t  dwReserved[4];
};

struct strlH {
  uint8_t   LIST[4] = {0x4C, 0x49, 0x53, 0x54};  //LIST
  uint32_t  dwSize;
  uint8_t   strl[4] = {0x73, 0x74, 0x72, 0x6C};  //strl
};

struct strhH {
  uint8_t   strh[4]         = {0x73, 0x74, 0x72, 0x68};  //strh
  uint32_t  dwSize          = sizeof(strhH) - 8;
  uint8_t   fccType[4]      = {0x76, 0x69, 0x64, 0x73}; // 'vids'
  uint8_t   fccHandler[4]   = {0x4D, 0x4A, 0x50, 0x47}; // 'MJPG'
  uint32_t  dwFlags;
  uint32_t  wPriority;
  uint32_t  dwInitialFrames;
  uint32_t  dwScale;
  uint32_t  dwRate;
  uint32_t  dwStart;
  uint32_t  dwLength;
  uint32_t  dwSuggestedBufferSize;
  uint32_t  dwQuality;
  uint32_t  dwSampleSize;
};

struct strfVH {
  uint8_t   strf[4]         = {0x73, 0x74, 0x72, 0x66};  //strf
  uint32_t  dwSize          = sizeof(strfVH) - 8;
  uint32_t  biSize          = sizeof(strfVH) - 8;
  uint32_t  biWidth;
  uint32_t  biHeight;
  uint16_t  biPlanes;
  uint16_t  biBitCount;
  uint8_t   biCompression[4] = {0x4D, 0x4A, 0x50, 0x47}; // 'MJPG'
  uint32_t  biSizeImage;
  uint32_t  biXPelsPerMeter;
  uint32_t  biYPelsPerMeter;
  uint32_t  biClrUsed;
  uint32_t  biClrImportant;
};

struct strfAH {
  uint8_t   strf[4]         = {0x73, 0x74, 0x72, 0x66};  //strf
  uint32_t  dwSize          = sizeof(strfVH) - 8;
  uint32_t  wFormatTag;
  uint32_t  nChannels;
  uint32_t  nSamplesPerSec;
  uint32_t  nAvgBytesPerSec;
  uint32_t  nBlockAlign;
  uint32_t  wBitsPerSample;
  uint32_t  cbSize;
};

struct moviH {
  uint8_t   LIST[4] = {0x4C, 0x49, 0x53, 0x54};  //LIST
  uint32_t  dwSize;
  uint8_t   movi[4] = {0x6D, 0x6F, 0x76, 0x69};  //movi
};

struct aviVideoHeader {
  riffH   riff;
  hdrlH   hdrl;
  avihH   avih;
  strlH   strl;
  strhH   strh;
  strfVH  strf;
};

struct aviAudioHeader {
  strlH   strl;
  strhH   strh;
  strfAH  strf;
};

struct frameSizeStruct {
  uint8_t frameWidth[2];
  uint8_t frameHeight[2];
};

CAVI::CAVI()
{
  idxBuf = NULL;
}

CAVI::~CAVI()
{
  closeFile("");
  cleanup();
}

int CAVI::startFile(const char* fileName, uint32_t fWidth, uint32_t fHeight, uint8_t FPS, bool audio, uint32_t maxFrameCount)
{
  closeFile("");
  cleanup();

  Serial.printf("Starting AVI [%s]: ", fileName);

  //copy filename for static information
  uint32_t nameLen = strlen(fileName);
  if(nameLen >= MAX_FILE_NAME) nameLen = MAX_FILE_NAME - 1;
  memcpy(cFileName, fileName, nameLen);
  cFileName[nameLen] = 0;

  //reset stats and store time
  startTime   = millis();
  frameCnt    = 0;
  fTimeTot    = 0;
  wTimeTot    = 0;
  dTimeTot    = 0;
  vidSize     = 0;
  highPoint   = 0;
  idxPtr      = 0;
  idxOffset   = 0;
  moviSize    = 0;
  audSize     = 0;
  indexLen    = 0;
  frameWidth  = fWidth;
  frameHeight = fHeight;
  maxFrames   = maxFrameCount;
  hasAudio    = audio;
  vFPS        = FPS;
  audioSampleRate = 0;

  //open the file and write temp header
  hFile = SD_MMC.open(fileName, FILE_WRITE);
  if(!hFile) {
    Serial.println("Unable to open");
    return AVI_RET_INVALID;
  }
  writeAviHdr(0);

  //allocate avi index
  idxBuf = (uint8_t*)ps_malloc((maxFrames+1)*IDX_ENTRY); // include some space for audio index
  if(idxBuf == NULL) {
    Serial.println("Unable to allocate index");
    return AVI_RET_ALLOC_ERROR;
  }
  memcpy(idxBuf, idx1Buf, 4); // index header
  idxPtr = CHUNK_HDR;  // leave 4 bytes for index size

  Serial.println("Success");

  return AVI_RET_OK;
}

int CAVI::closeFile(const char* audioFileName)
{
  //check if file is open
  if(!hFile) {
    cleanup();
    return AVI_RET_NOT_OPEN;
  }

  // closes the recorded file
  uint32_t cTime = millis();
  uint32_t vidDuration = cTime - startTime;
  uint32_t vidDurationSecs = lround(vidDuration/1000.0);
  Serial.printf("Capture time %u s\n", vidDurationSecs);
  
  // write remaining frame content to SD
  hFile.write(iSDbuffer, highPoint); 
  size_t readLen = 0;

  //write WAV file
  if(hasAudio) {
    Serial.print("Audio: ");
    int wavReturn = writeWavFile(audioFileName);
    if(wavReturn != AVI_RET_OK) {
      Serial.println("Failed");
      cleanup();
      return wavReturn;
    }
    Serial.println("Added");
  }
  
  //write avi index
  writeAviIndex();
  
  // save avi header at start of file
  float actualFPS = (1000.0f * (float)frameCnt) / ((float)vidDuration);
  writeAviHdr(actualFPS);

  //close file and delete buffer
  cleanup();

  //find time required to save file
  uint32_t hTime = millis();
  Serial.printf("Final SD storage time %lu ms\n", hTime - cTime);
  cTime = millis() - cTime;
    
  // AVI stats
  Serial.printf("******** AVI recording stats ********\n");
  Serial.printf("Recorded %s\n", cFileName);
  Serial.printf("AVI duration: %u secs\n", vidDurationSecs);
  Serial.printf("Number of frames: %u\n", frameCnt);
  Serial.printf("Required FPS: %u\n", vFPS);
  Serial.printf("Actual FPS: %0.1f\n", actualFPS);
  Serial.printf("File size: %s\n", fmtSize(vidSize));
  if (frameCnt) {
    Serial.printf("Average frame length: %u bytes\n", vidSize / frameCnt);
    Serial.printf("Average frame monitoring time: %u ms\n", dTimeTot / frameCnt);
    Serial.printf("Average frame buffering time: %u ms\n", fTimeTot / frameCnt);
    Serial.printf("Average frame storage time: %u ms\n", wTimeTot / frameCnt);
    Serial.printf("Average SD write speed: %u kB/s\n", ((vidSize / wTimeTot) * 1000) / 1024);
    Serial.printf("Busy: %u%%\n", std::min(100 * (wTimeTot + fTimeTot + dTimeTot + cTime) / vidDuration, (uint32_t)100));
  }
  Serial.printf("Completion time: %u ms\n", cTime);
  Serial.printf("*************************************\n");
  
  return AVI_RET_OK;
}

int CAVI::writeFrame(camera_fb_t* fb)
{
  if(!hFile) {
    Serial.println("Unable to write frame: File is not open");
    return AVI_RET_NOT_OPEN;
  }

  //If we have reached maxFrames close the file
  if(frameCnt == maxFrames) {
    Serial.println("Unable to write frame: Reached max frames");
    return AVI_RET_MAX_FRAME;
  }
  
  // save frame on SD card
  uint32_t fTime = millis();
  
  // align end of jpeg on 4 byte boundary for AVI
  uint16_t filler = (4 - (fb->len & 0x00000003)) & 0x00000003; 
  size_t jpegSize = fb->len + filler;
  
  // add avi frame header
  memcpy(iSDbuffer + highPoint, dcBuf, 4); 
  memcpy(iSDbuffer + highPoint + 4, &jpegSize, 4);
  highPoint += CHUNK_HDR;
  if (highPoint >= RAMSIZE) {
    // marker overflows buffer
    highPoint -= RAMSIZE;
    hFile.write(iSDbuffer, RAMSIZE);
    
    // push overflow to buffer start
    memcpy(iSDbuffer, iSDbuffer + RAMSIZE, highPoint);
  }
  
  // add frame content
  size_t jpegRemain = jpegSize;
  uint32_t wTime = millis();
  while (jpegRemain >= RAMSIZE - highPoint) {
    // write to SD when RAMSIZE is filled in buffer
    memcpy(iSDbuffer + highPoint, fb->buf + jpegSize - jpegRemain, RAMSIZE - highPoint);
    hFile.write(iSDbuffer, RAMSIZE);
    jpegRemain -= RAMSIZE - highPoint;
    highPoint = 0;
  } 
  wTime = millis() - wTime;
  wTimeTot += wTime;
  
  // whats left or small frame
  memcpy(iSDbuffer+highPoint, fb->buf + jpegSize - jpegRemain, jpegRemain);
  highPoint += jpegRemain;
  addAviIndex(jpegSize); // save avi index for frame
  vidSize += jpegSize + CHUNK_HDR;
  frameCnt++; 
  fTime = millis() - fTime - wTime;
  fTimeTot += fTime;
  Serial.printf("Frame [%d] processing time %u ms, storage time %u ms\n", frameCnt, fTime, wTime);
  
  return AVI_RET_OK;
}

bool CAVI::isOpen()
{
  if(!hFile) {
    return false;
  }

  return true;
}

void CAVI::addAviIndex(uint32_t dataSize)
{
  // build AVI video index into buffer - 16 bytes per frame
  // called from saveFrame() for each frame
  moviSize += dataSize;
  memcpy(idxBuf + idxPtr, dcBuf, 4);
  memcpy(idxBuf + idxPtr + 4, zeroBuf, 4);
  memcpy(idxBuf + idxPtr + 8, &idxOffset, 4); 
  memcpy(idxBuf + idxPtr + 12, &dataSize, 4); 
  idxOffset += dataSize + CHUNK_HDR;
  idxPtr += IDX_ENTRY; 
}

void CAVI::writeAviIndex()
{
  // update index with size
  uint32_t sizeOfIndex = (frameCnt + (hasAudio ? 1 : 0)) * IDX_ENTRY;
  memcpy(idxBuf + 4, &sizeOfIndex, 4); // size of index 
  indexLen = sizeOfIndex + CHUNK_HDR;
  idxPtr = 0; // pointer to index buffer
  
  uint32_t readLen = 0;
  do {
    readLen = indexLen - idxPtr;
    if(readLen < 0) readLen = 0;
    if(readLen > RAMSIZE) readLen = RAMSIZE;
    memcpy(iSDbuffer, idxBuf + idxPtr, readLen);
    hFile.write(iSDbuffer, readLen);
    idxPtr += readLen;
  } while (readLen > 0);
  
  idxPtr = 0;
}

int CAVI::writeWavFile(const char* fileName)
{
  //check for audio file
  if(fileName && strlen(fileName)) {
    //open file
    File hAudio = SD_MMC.open(fileName, FILE_READ);
    if(!hAudio) {
      Serial.println("Unable to open");
      return AVI_RET_NOT_FOUND;
    }

    // add sound file index
    audSize = hAudio.size() - WAV_HEADER_LEN;
    addAviIndex(audSize);
    hAudio.seek(WAV_HEADER_LEN, SeekSet); // skip over header

    //write data
    uint32_t readLen = 0;
    uint8_t offset = CHUNK_HDR;
    memcpy(iSDbuffer, wbBuf, 4);     
    memcpy(iSDbuffer + 4, &audSize, 4);
    do {
      readLen = hAudio.read(iSDbuffer + offset, RAMSIZE - offset) + offset; 
      hFile.write(iSDbuffer, readLen);
      offset = 0;
    } while (readLen > 0);

    hAudio.close();
    
    hasAudio = true;

    return AVI_RET_OK;
  }

  return AVI_RET_INVALID;
}

void CAVI::writeAviHdr(float actualFPS)
{
  aviVideoHeader vHeader;
  vHeader.riff.dwSize                 = moviSize + AVI_HEADER_LEN + ((CHUNK_HDR + IDX_ENTRY) * (frameCnt + (hasAudio ? 1 : 0)));
  vHeader.hdrl.dwSize                 = sizeof(aviVideoHeader) + (hasAudio ? sizeof(aviAudioHeader) : 0) - 20;
  vHeader.avih.dwMicroSecPerFrame     = (uint32_t)round(1000000.0f / actualFPS); // usecs_per_frame
  vHeader.avih.dwMaxBytesPerSec       = 1000000;
  vHeader.avih.dwPaddingGranularity   = 0;
  vHeader.avih.dwFlags                = 16;
  vHeader.avih.dwTotalFrames          = frameCnt;
  vHeader.avih.dwInitialFrames        = 0;
  vHeader.avih.dwStreams              = 1;
  vHeader.avih.dwSuggestedBufferSize  = 500000;
  vHeader.avih.dwWidth                = frameWidth;
  vHeader.avih.dwHeight               = frameHeight;
  vHeader.avih.dwReserved[0]          = 0;
  vHeader.avih.dwReserved[1]          = 0;
  vHeader.avih.dwReserved[2]          = 0;
  vHeader.avih.dwReserved[3]          = 0;
  vHeader.strl.dwSize                 = sizeof(strhH) + sizeof(strfVH) + 4;  
  vHeader.strh.dwFlags                = 0x0;
  vHeader.strh.wPriority              = 0;
  vHeader.strh.dwInitialFrames        = 0;
  vHeader.strh.dwScale                = 100;
  vHeader.strh.dwRate                 = (uint32_t)round(100.0f * actualFPS);
  vHeader.strh.dwStart                = 0;
  vHeader.strh.dwLength               = frameCnt;
  vHeader.strh.dwSuggestedBufferSize  = 500000;
  vHeader.strh.dwQuality              = 0;
  vHeader.strh.dwSampleSize           = 0;
  vHeader.strf.biWidth                = frameWidth;
  vHeader.strf.biHeight               = frameHeight;
  vHeader.strf.biPlanes               = 1;
  vHeader.strf.biBitCount             = 24;
  vHeader.strf.biSizeImage            = 0;
  vHeader.strf.biXPelsPerMeter        = 0;
  vHeader.strf.biYPelsPerMeter        = 0;
  vHeader.strf.biClrUsed              = 0;
  vHeader.strf.biClrImportant         = 0;
  hFile.seek(0, SeekSet); // start of file
  hFile.write((uint8_t*)&vHeader, sizeof(aviVideoHeader));

  if(hasAudio) {
    aviAudioHeader aHeader;
    aHeader.strl.dwSize           = sizeof(strhH) + sizeof(strfAH) + 4;
    aHeader.strf.wFormatTag       = 0;
    aHeader.strf.nChannels        = 0;
    aHeader.strf.nSamplesPerSec   = 0;
    aHeader.strf.nAvgBytesPerSec  = 0;
    aHeader.strf.nBlockAlign      = 0;
    aHeader.strf.wBitsPerSample   = 0;
    aHeader.strf.cbSize           = 0;
    hFile.write((uint8_t*)&aHeader, sizeof(aviAudioHeader));
  }

  moviH moviHeader;
  moviHeader.dwSize = moviSize + ((frameCnt + (hasAudio ? 1 : 0)) * CHUNK_HDR) + 4;
  hFile.write((uint8_t*)&moviHeader, sizeof(moviH));
}

void CAVI::cleanup()
{
  //delete buffer
  if(idxBuf) {
    free(idxBuf);
    idxBuf = NULL;
  }

  if(hFile) {
    hFile.close();
  }
}

char* CAVI::fmtSize(uint64_t sizeVal)
{
  // format size according to magnitude
  // only one call per format string
  static char returnStr[20];
  if (sizeVal < 100 * 1024) sprintf(returnStr, "%llu bytes", sizeVal);
  else if (sizeVal < 1024 * 1024) sprintf(returnStr, "%llukB", sizeVal / 1024);
  else if (sizeVal < 1024 * 1024 * 1024) sprintf(returnStr, "%0.1fMB", (double)(sizeVal) / (1024 * 1024));
  else sprintf(returnStr, "%0.1fGB", (double)(sizeVal) / (1024 * 1024 * 1024));
  return returnStr;
}
