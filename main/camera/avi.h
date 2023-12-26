#ifndef AVI_H
#define AVI_H

#include "esp_camera.h"
#include "fat32.h"

#define AVI_RET_OK            0
#define AVI_RET_INVALID       1
#define AVI_RET_NOT_OPEN      2
#define AVI_RET_NOT_FOUND     3
#define AVI_RET_ALLOC_ERROR   4
#define AVI_RET_MAX_FRAME     5

#define WAV_HEADER_LEN        44 // WAV header length
#define RAMSIZE               (1024 * 8) // set this to multiple of SD card sector size (512 or 1024 bytes)
#define CHUNKSIZE             (1024 * 4)
#define AVI_HEADER_LEN        310 // AVI header length
#define IDX_ENTRY             16 // bytes per index entry
#define CHUNK_HDR             8 // bytes per jpeg hdr in AVI 
#define MAX_FILE_NAME         256

class CAVI {
public:
    CAVI();
    ~CAVI();
    
    int       startFile(const char* fileName, uint32_t fWidth, uint32_t fHeight, uint8_t FPS, bool audio, uint32_t maxFrameCount);
    int       closeFile(const char* audioFileName);
    int       writeFrame(camera_fb_t* fb);
    bool      isOpen();
    
private:
    void        addAviIndex(uint32_t dataSize);
    void        writeAviIndex();
    int         writeWavFile(const char* fileName);
    void        writeAviHdr(float actualFPS);
    char*       fmtSize(uint64_t sizeVal);
    void        cleanup();

    char        cFileName[MAX_FILE_NAME];
    FILE*       hFile;
    uint32_t    startTime;
    uint32_t    frameCnt;
    uint32_t    fTimeTot;
    uint32_t    wTimeTot;
    uint32_t    dTimeTot;
    uint32_t    vidSize;
    uint32_t    highPoint;
    uint32_t    idxPtr;
    uint32_t    idxOffset;
    uint8_t*    idxBuf;
    uint32_t    moviSize;
    uint32_t    audSize;
    uint32_t    indexLen;
    uint32_t    frameWidth;
    uint32_t    frameHeight;
    uint32_t    maxFrames;
    bool        hasAudio;
    uint8_t     vFPS;
    uint32_t    audioSampleRate;
    uint8_t     iSDbuffer[(RAMSIZE + CHUNK_HDR) * 2];
    char        fmtString[20];
};

#endif
