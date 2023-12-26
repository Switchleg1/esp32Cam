#ifndef MOTION_H
#define MOTION_H

#include "globals.h"

#define MAX_IMAGE_SIZE  32*1024

class CMotion {
  public:
    CMotion();
    ~CMotion();

    void        setImageParameters(uint32_t newScaleFactor, uint32_t newSampleRate, uint32_t newFrameWidth, uint32_t newFrameHeight);
    void        setDetectionParameters(uint32_t motionFrames, uint32_t nightFrames, uint32_t threshold);
    bool        checkMotion(camera_fb_t* fb);
    bool        getMotion();
    bool        fetchMoveMap(uint8_t **out, size_t *out_len);
    bool        isNight(uint8_t nightSwitch);
    
  private:
    int         detectMotionFrames;
    int         detectNightFrames;
    int         detectNumBands;
    int         detectStartBand;
    int         detectEndBand;
    int         detectChangeThreshold;

    uint8_t     lightLevel;
    uint8_t     nightSwitch;
    float       motionVal;
    size_t      jpgImgSize;

    uint32_t    scaleFactor;
    uint32_t    sampleRate;
    uint32_t    frameWidth;
    uint32_t    frameHeight;
    
    bool        dbgMotion;
    bool        motionStatus;

    uint32_t    motionCnt;
    size_t      lastImgLen;

    bool        nightTime;
    uint16_t    nightCnt;

    uint8_t*    changeMap;
    uint8_t*    prevBuf;
    uint8_t*    jpgImg;
};

#endif
