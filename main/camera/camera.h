#ifndef CAMERA_H
#define CAMERA_H

#include "globals.h"
#include "avi.h"
#include "motion.h"

//Task configuration
#define TRIG_TASK_PRIO          10
#define CAPT_TSK_PRIO           2
#define MOTI_TSK_PRIO           1
#define TASK_STACK_SIZE         4096
#define TIMEOUT_TASK            250

//Return values
#define CAM_RET_OK              0
#define CAM_RET_INIT_FAIL       1
#define CAM_RET_FB_INVALID      2
#define CAM_RET_FILE_NOT_OPEN   3

#define CAM_MAX_FRAMES          1000
#define CAM_COUNT_DOWN          50

class CCamera {
  public:
    CCamera();
    ~CCamera();
    
    int                 start();
    int                 stop();
    int                 startFile(const char* fileName, uint32_t maxFrameCount);
    int                 closeFile();
    bool                isRecording();
    bool                isRunning();
    bool                getAllowMotion();
    void                setAllowMotion(bool allow);
    void                setOnFrameCallback(bool (*cb)(camera_fb_t*));
    
  private:
    static void         cameraTriggerTask(void* vPtr);
    static void         cameraCaptureTask(void* vPtr);
    static void         cameraMotionTask(void* vPtr);
    
    void                setupLedFlash(int pin);

    CAVI                aviFile;
    CMotion             motion;
    bool                allowTasks;
    bool                allowMotion;
    SemaphoreHandle_t   triggerTaskMutex;
    SemaphoreHandle_t   captureTaskMutex;
    SemaphoreHandle_t   motionTaskMutex;
    SemaphoreHandle_t   syncTaskSemaphore;
    SemaphoreHandle_t   triggerSemaphore;
    QueueHandle_t       motionQueue;
    uint32_t            recordCountDown;
    bool                (*onFrame)(camera_fb_t*);
};

extern CCamera Camera;

#endif
