#ifndef MOTION_H
#define MOTION_H

class CMotion {
  public:
    CMotion();
    ~CMotion();

    void      setParameters(uint32_t newScaleFactor, uint32_t newSampleRate, uint32_t newFrameWidth, uint32_t newFrameHeight);
    bool      checkMotion(camera_fb_t* fb);
    bool      getMotion();
    bool      fetchMoveMap(uint8_t **out, size_t *out_len);
    bool      isNight(uint8_t nightSwitch);
    
  private:
    int       detectMotionFrames;
    int       detectNightFrames;
    int       detectNumBands;
    int       detectStartBand;
    int       detectEndBand;
    int       detectChangeThreshold;

    uint8_t   lightLevel;
    uint8_t   nightSwitch;
    float     motionVal;
    uint8_t*  jpgImg;
    size_t    jpgImgSize;

    uint32_t  scaleFactor;
    uint32_t  sampleRate;
    uint32_t  frameWidth;
    uint32_t  frameHeight;
    
    bool      dbgMotion;
    bool      nightTime;
    bool      motionStatus;
};

#endif
