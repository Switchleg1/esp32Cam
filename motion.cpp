#include <arduino.h>
#include <esp_camera.h>
#include "motion.h"
#include "jpg2rgb.h"
#include "globals.h"

CMotion::CMotion()
{
  detectMotionFrames = 5; // min sequence of changed frames to confirm motion 
  detectNightFrames = 10; // frames of sequential darkness to avoid spurious day / night switching
  // define region of interest, ie exclude top and bottom of image from movement detection if required
  // divide image into detectNumBands horizontal bands, define start and end bands of interest, 1 = top
  detectNumBands = 10;
  detectStartBand = 3;
  detectEndBand = 8; // inclusive
  detectChangeThreshold = 15; // min difference in pixel comparison to indicate a change

  lightLevel = 0; // Current ambient light level 
  nightSwitch = 20; // initial white level % for night/day switching
  motionVal = 8.0; // initial motion sensitivity setting
  jpgImg = NULL;
  jpgImgSize = 0;

  scaleFactor = 1;
  sampleRate  = 1;
  frameWidth  = 96;
  frameHeight = 96;

  nightTime   = false;
  dbgMotion   = false;
}

CMotion::~CMotion()
{
  if(jpgImg) {
    free(jpgImg);
  }
}

void CMotion::setParameters(uint32_t newScaleFactor, uint32_t newSampleRate, uint32_t newFrameWidth, uint32_t newFrameHeight)
{
  scaleFactor = newScaleFactor;
  sampleRate  = newSampleRate;
  frameWidth  = newFrameWidth;
  frameHeight = newFrameHeight;
}

bool CMotion::checkMotion(camera_fb_t* fb)
{
  // check difference between current and previous image (subtract background)
  // convert image from JPEG to downscaled RGB888 bitmap to 8 bit grayscale
  uint32_t dTime = millis();
  uint32_t lux = 0;
  static uint32_t motionCnt = 0;
  uint8_t* rgb_buf = NULL;
  uint8_t* jpg_buf = NULL;
  size_t jpg_len = 0;

  // calculate parameters for sample size
  uint8_t scaling = scaleFactor; 
  uint16_t reducer = sampleRate;
  uint8_t downsize = pow(2, scaling) * reducer;
  uint32_t sampleWidth = frameWidth / downsize;
  uint32_t sampleHeight = frameHeight / downsize;
  uint32_t num_pixels = sampleWidth * sampleHeight;
  if (!jpg2rgb((uint8_t*)fb->buf, fb->len, &rgb_buf, (jpg_scale_t)scaling)) {
    Serial.println("motionDetect: jpg2rgb() failed");
    free(rgb_buf);
    rgb_buf = NULL;
    return motionStatus;
  }

/*
  if (reducer > 1) 
    // further reduce size of bitmap 
    for (int r=0; r<sampleHeight; r++) 
      for (int c=0; c<sampleWidth; c++)      
        rgb_buf[c+(r*sampleWidth)] = rgb_buf[(c+(r*sampleWidth))*reducer]; 
*/
  //Serial.printf("JPEG to greyscale conversion %u bytes in %ums\n", num_pixels, millis() - dTime);
  dTime = millis();

  // allocate buffer space on heap
  int maxSize = 32*1024; // max size downscaled UXGA 30k
  static uint8_t* changeMap = (uint8_t*)ps_malloc(maxSize);
  static uint8_t* prev_buf = (uint8_t*)ps_malloc(maxSize);
  static uint8_t* _jpgImg = (uint8_t*)ps_malloc(maxSize);
  jpgImg = _jpgImg;

  // compare each pixel in current frame with previous frame 
  int changeCount = 0;
  // set horizontal region of interest in image 
  uint16_t startPixel = num_pixels*(detectStartBand-1)/detectNumBands;
  uint16_t endPixel = num_pixels*(detectEndBand)/detectNumBands;
  int moveThreshold = (endPixel-startPixel) * (11-motionVal)/100; // number of changed pixels that constitute a movement
  for (int i=0; i<num_pixels; i++) {
    if (abs((int)rgb_buf[i] - (int)prev_buf[i]) > detectChangeThreshold) {
      if (i > startPixel && i < endPixel) changeCount++; // number of changed pixels
      if (dbgMotion) changeMap[i] = 192; // populate changeMap image with changed pixels in gray
    } else if (dbgMotion) changeMap[i] =  255; // set white 
    lux += rgb_buf[i]; // for calculating light level
  }
  lightLevel = (lux*100)/(num_pixels*255); // light value as a %
  nightTime = isNight(nightSwitch);
  memcpy(prev_buf, rgb_buf, num_pixels); // save image for next comparison 
  // esp32-cam issue #126
  if (rgb_buf == NULL) {
    Serial.printf("Memory leak, heap now: %u, pSRAM now: %u\n", ESP.getFreeHeap(), ESP.getFreePsram());
  }
  free(rgb_buf); 
  rgb_buf = NULL;
  //Serial.printf("Detected %u changes, threshold %u, light level %u, in %lums\n", changeCount, moveThreshold, lightLevel, millis() - dTime);
  dTime = millis();

  if (changeCount > moveThreshold) {
    Serial.println("### Change detected");
    motionCnt++; // number of consecutive changes
    // need minimum sequence of changes to signal valid movement
    if (!motionStatus && motionCnt >= detectMotionFrames) {
      Serial.println("***** Motion - START");
      motionStatus = true; // motion started
    } 
    if (dbgMotion)
      // to highlight movement detected in changeMap image, set all gray in region of interest to black
      for (int i=0; i<num_pixels; i++) 
         if (i > startPixel && i < endPixel && changeMap[i] < 255) changeMap[i] = 0;
  } else {
    // insufficient change
    if (motionStatus) {
      Serial.printf("***** Motion - STOP after %u frames\n", motionCnt);
      motionCnt = 0;
      motionStatus = false; // motion stopped
    }
  }
  
  if (motionStatus) {
    Serial.printf("*** Motion - ongoing %u frames\n", motionCnt);
  }

  if (dbgMotion) { 
    // build jpeg of changeMap for debug streaming
    dTime = millis();
    if (!fmt2jpg(changeMap, num_pixels, sampleWidth, sampleHeight, PIXFORMAT_GRAYSCALE, 80, &jpg_buf, &jpg_len)) {
      Serial.println("motionDetect: fmt2jpg() failed");
    }
    // prevent streaming from accessing jpeg while it is being updated
    memcpy(jpgImg, jpg_buf, jpg_len);
    jpgImgSize = jpg_len; 
    free(jpg_buf);
    jpg_buf = NULL;
    //Serial.printf("Created changeMap JPEG %d bytes in %lums\n", jpg_len, millis() - dTime);
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
  static size_t lastImgLen = 0;
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
  static bool nightTime = false;
  static uint16_t nightCnt = 0;
  if (nightTime) {
    if (lightLevel > nightSwitch) {
      // light image
      nightCnt--;
      // signal day time after given sequence of light frames
      if (nightCnt == 0) {
        nightTime = false;
        Serial.println("Day time");
      }
    }
  } else {
    if (lightLevel < nightSwitch) {
      // dark image
      nightCnt++;
      // signal night time after given sequence of dark frames
      if (nightCnt > detectNightFrames) {
        nightTime = true;     
        Serial.println("Night time"); 
      }
    }
  } 
  return nightTime;
}
