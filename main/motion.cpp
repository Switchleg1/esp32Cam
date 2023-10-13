#include <stdio.h>
#include <math.h>
#include <string.h>
#include "motion.h"
#include "jpg2rgb.h"
#include "currenttime.h"

#define CMOT_TAG "CMotion"

CMotion::CMotion()
{
    detectMotionFrames      = 5; // min sequence of changed frames to confirm motion 
    detectNightFrames       = 10; // frames of sequential darkness to avoid spurious day / night switching
    // define region of interest, ie exclude top and bottom of image from movement detection if required
    // divide image into detectNumBands horizontal bands, define start and end bands of interest, 1 = top
    detectNumBands          = 10;
    detectStartBand         = 3;
    detectEndBand           = 8; // inclusive
    detectChangeThreshold   = 15; // min difference in pixel comparison to indicate a change

    lightLevel              = 0; // Current ambient light level 
    nightSwitch             = 20; // initial white level % for night/day switching
    motionVal               = 8.0; // initial motion sensitivity setting
    jpgImgSize              = 0;

    scaleFactor             = 1;
    sampleRate              = 1;
    frameWidth              = 96;
    frameHeight             = 96;

    dbgMotion               = false;
    motionStatus            = false;

    motionCnt               = 0;
    lastImgLen              = 0;

    nightTime               = false;
    nightCnt                = 0;

    changeMap               = (uint8_t*)malloc(MAX_IMAGE_SIZE);
    prevBuf                 = (uint8_t*)malloc(MAX_IMAGE_SIZE);
    jpgImg                  = (uint8_t*)malloc(MAX_IMAGE_SIZE);
}

CMotion::~CMotion()
{
    if(changeMap) {
        free(changeMap);
    }

    if (jpgImg) {
        free(jpgImg);
    }

    if (prevBuf) {
        free(prevBuf);
    }
}

void CMotion::setImageParameters(uint32_t newScaleFactor, uint32_t newSampleRate, uint32_t newFrameWidth, uint32_t newFrameHeight)
{
    scaleFactor = newScaleFactor;
    sampleRate  = newSampleRate;
    frameWidth  = newFrameWidth;
    frameHeight = newFrameHeight;
}

void CMotion::setDetectionParameters(uint32_t motionFrames, uint32_t nightFrames, uint32_t threshold)
{
    detectMotionFrames      = motionFrames;
    detectNightFrames       = nightFrames;
    detectChangeThreshold   = threshold;
}

bool CMotion::checkMotion(camera_fb_t* fb)
{
    // check difference between current and previous image (subtract background)
    // convert image from JPEG to downscaled RGB888 bitmap to 8 bit grayscale
    uint32_t dTime  = CurrentTime.ms();
    uint32_t lux    = 0;
    uint8_t* rgbBuf = NULL;
    uint8_t* jpgBuf = NULL;
    size_t jpg_len  = 0;

    // calculate parameters for sample size
    uint8_t scaling = scaleFactor; 
    uint16_t reducer = sampleRate;
    uint8_t downsize = pow(2, scaling) * reducer;
    uint32_t sampleWidth = frameWidth / downsize;
    uint32_t sampleHeight = frameHeight / downsize;
    uint32_t num_pixels = sampleWidth * sampleHeight;
    if (!jpg2rgb((uint8_t*)fb->buf, fb->len, &rgbBuf, (jpg_scale_t)scaling)) {
        ESP_LOGE(CMOT_TAG, "checkMotion: motionDetect: jpg2rgb() failed");
        free(rgbBuf);
        rgbBuf = NULL;

        return motionStatus;
    }

    /*
      if (reducer > 1) 
        // further reduce size of bitmap 
        for (int r=0; r<sampleHeight; r++) 
          for (int c=0; c<sampleWidth; c++)      
            rgb_buf[c+(r*sampleWidth)] = rgb_buf[(c+(r*sampleWidth))*reducer]; 
    */
    ESP_LOGD(CMOT_TAG, "checkMotion: JPEG to greyscale conversion %lu bytes in %lums", num_pixels, CurrentTime.ms() - dTime);
    dTime = CurrentTime.ms();

    // compare each pixel in current frame with previous frame 
    int changeCount = 0;
    // set horizontal region of interest in image 
    uint16_t startPixel = num_pixels*(detectStartBand-1)/detectNumBands;
    uint16_t endPixel = num_pixels*(detectEndBand)/detectNumBands;
    int moveThreshold = (endPixel-startPixel) * (11-motionVal)/100; // number of changed pixels that constitute a movement
    for (int i=0; i<num_pixels; i++) {
        if (abs((int)rgbBuf[i] - (int)prevBuf[i]) > detectChangeThreshold) {
            if (i > startPixel && i < endPixel) changeCount++; // number of changed pixels
            if (dbgMotion) changeMap[i] = 192; // populate changeMap image with changed pixels in gray
        } else if (dbgMotion) changeMap[i] =  255; // set white 

        lux += rgbBuf[i]; // for calculating light level
    }
    lightLevel = (lux*100)/(num_pixels*255); // light value as a %
    nightTime = isNight(nightSwitch);
    memcpy(prevBuf, rgbBuf, num_pixels); // save image for next comparison 
    // esp32-cam issue #126
    if (rgbBuf == NULL) {
        ESP_LOGE(CMOT_TAG, "checkMotion: Memory leak, heap now: %u, pSRAM now: %u", heap_caps_get_free_size(MALLOC_CAP_8BIT), heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
    free(rgbBuf);
    rgbBuf = NULL;
    ESP_LOGD(CMOT_TAG, "checkMotion: Detected %u changes, threshold %u, light level %u, in %lums", changeCount, moveThreshold, lightLevel, CurrentTime.ms() - dTime);
    dTime = CurrentTime.ms();

    if (changeCount > moveThreshold) {
        ESP_LOGI(CMOT_TAG, "checkMotion: ### Change detected");
        motionCnt++; // number of consecutive changes
        // need minimum sequence of changes to signal valid movement
        if (!motionStatus && motionCnt >= detectMotionFrames) {
            ESP_LOGI(CMOT_TAG, "checkMotion: ***** Motion - START");
            motionStatus = true; // motion started
        } 

        if (dbgMotion)
            // to highlight movement detected in changeMap image, set all gray in region of interest to black
            for (int i=0; i<num_pixels; i++) 
                if (i > startPixel && i < endPixel && changeMap[i] < 255) changeMap[i] = 0;

    } else {
        // insufficient change
        if (motionStatus) {
            ESP_LOGI(CMOT_TAG, "checkMotion: ***** Motion - STOP after %lu frames", motionCnt);
            motionCnt = 0;
            motionStatus = false; // motion stopped
        }
    }
  
    if (motionStatus) {
        ESP_LOGI(CMOT_TAG, "checkMotion: *** Motion - ongoing %lu frames", motionCnt);
    }

    if (dbgMotion) { 
        // build jpeg of changeMap for debug streaming
        dTime = CurrentTime.ms();
        if (!fmt2jpg(changeMap, num_pixels, sampleWidth, sampleHeight, PIXFORMAT_GRAYSCALE, 80, &jpgBuf, &jpg_len)) {
            ESP_LOGE(CMOT_TAG, "checkMotion: fmt2jpg() failed");
        }
        // prevent streaming from accessing jpeg while it is being updated
        memcpy(jpgImg, jpgBuf, jpg_len);
        jpgImgSize = jpg_len; 
        free(jpgBuf);
        jpgBuf = NULL;
        ESP_LOGD(CMOT_TAG, "checkMotion: Created changeMap JPEG %d bytes in %lums", jpg_len, CurrentTime.ms() - dTime);
    }
   
    // motionStatus indicates whether motion previously ongoing or not
    return nightTime ? false : motionStatus;
}

bool CMotion::getMotion()
{
  return motionStatus;
}

bool CMotion::fetchMoveMap(uint8_t **out, size_t *out_len)
{
    // return change map jpeg for streaming               
    *out = jpgImg;
    *out_len = jpgImgSize;

    if (lastImgLen != jpgImgSize) {
        // image changed
        lastImgLen = jpgImgSize;

        return true;
    }
  
    return false;
}

bool CMotion::isNight(uint8_t nightSwitch)
{
    // check if night time for suspending recording
    // or for switching on lamp if enabled
    if (nightTime) {
        if (lightLevel > nightSwitch) {
            // light image
            nightCnt--;
            // signal day time after given sequence of light frames
            if (nightCnt == 0) {
                nightTime = false;
                ESP_LOGI(CMOT_TAG, "isNight: Day time");
            }
        }
    } else {
        if (lightLevel < nightSwitch) {
            // dark image
            nightCnt++;
            // signal night time after given sequence of dark frames
            if (nightCnt > detectNightFrames) {
                nightTime = true;     
                ESP_LOGI(CMOT_TAG, "isNight: Night time"); 
            }
        }
    }

    return nightTime;
}
