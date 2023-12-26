#include "camera.h"
#include "currenttime.h"

#define CCAMERA_TAG "CCamera"

CCamera Camera;

struct frameStruct {
    const char* frameSizeStr;
    const uint16_t frameWidth;
    const uint16_t frameHeight;
    const uint16_t defaultFPS;
    const uint8_t scaleFactor; // (0..4)
    const uint8_t sampleRate; // (1..N)
};

const frameStruct frameData[] = {
    {"96X96", 96, 96, 30, 1, 1},   // 2MP sensors
    {"QQVGA", 160, 120, 30, 1, 1},
    {"QCIF", 176, 144, 30, 1, 1}, 
    {"HQVGA", 240, 176, 30, 2, 1}, 
    {"240X240", 240, 240, 30, 2, 1}, 
    {"QVGA", 320, 240, 30, 2, 1}, 
    {"CIF", 400, 296, 30, 2, 1},  
    {"HVGA", 480, 320, 30, 2, 1}, 
    {"VGA", 640, 480, 20, 3, 1}, 
    {"SVGA", 800, 600, 20, 3, 1}, 
    {"XGA", 1024, 768, 5, 3, 1},   
    {"HD", 1280, 720, 5, 3, 1}, 
    {"SXGA", 1280, 1024, 5, 3, 1}, 
    {"UXGA", 1600, 1200, 10, 3, 1},  
    {"FHD", 920, 1080, 5, 3, 1},    // 3MP Sensors
    {"P_HD", 720, 1280, 5, 3, 1},
    {"P_3MP", 864, 1536, 5, 3, 1},
    {"QXGA", 2048, 1536, 5, 4, 1},
    {"QHD", 2560, 1440, 5, 4, 1},   // 5MP Sensors
    {"WQXGA", 2560, 1600, 5, 4, 1},
    {"P_FHD", 1080, 1920, 5, 3, 1},
    {"QSXGA", 2560, 1920, 4, 4, 1}
};

CCamera::CCamera()
{
    captureTaskMutex    = xSemaphoreCreateMutex();
    triggerTaskMutex    = xSemaphoreCreateMutex();
    motionTaskMutex     = xSemaphoreCreateMutex();
    syncTaskSemaphore   = xSemaphoreCreateBinary();
    triggerSemaphore    = xSemaphoreCreateCounting(3, 0);
    motionQueue         = xQueueCreate(1, sizeof(camera_fb_t*));
    allowMotion         = true;
    onFrame             = NULL;
}

CCamera::~CCamera()
{
    stop();

    if(motionQueue) {
        vQueueDelete(motionQueue);
    }

    if(captureTaskMutex) {
        vSemaphoreDelete(captureTaskMutex);
    }

    if(triggerTaskMutex) {
        vSemaphoreDelete(triggerTaskMutex);
    }

    if(motionTaskMutex) {
        vSemaphoreDelete(motionTaskMutex);
    }

    if(syncTaskSemaphore) {
        vSemaphoreDelete(syncTaskSemaphore);
    }

    if(triggerSemaphore) {
        vSemaphoreDelete(triggerSemaphore);
    }
}

int CCamera::start()
{
    stop();
  
    ESP_LOGD(CCAMERA_TAG, "start: Starting Camera");
  
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer   = LEDC_TIMER_0;
    config.pin_d0       = Y2_GPIO_NUM;
    config.pin_d1       = Y3_GPIO_NUM;
    config.pin_d2       = Y4_GPIO_NUM;
    config.pin_d3       = Y5_GPIO_NUM;
    config.pin_d4       = Y6_GPIO_NUM;
    config.pin_d5       = Y7_GPIO_NUM;
    config.pin_d6       = Y8_GPIO_NUM;
    config.pin_d7       = Y9_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_pclk     = PCLK_GPIO_NUM;
    config.pin_vsync    = VSYNC_GPIO_NUM;
    config.pin_href     = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.frame_size   = FRAMESIZE_UXGA;
    config.pixel_format = PIXFORMAT_JPEG; // for streaming
    config.grab_mode    = CAMERA_GRAB_LATEST; //CAMERA_GRAB_WHEN_EMPTY;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    config.jpeg_quality = 10;
    config.fb_count     = 3;

#if defined(CAMERA_MODEL_ESP_EYE)
    pinMode(13, INPUT_PULLUP);
    pinMode(14, INPUT_PULLUP);
#endif

    // camera init
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(CCAMERA_TAG, "start: Failed [0x%x]", err);
    
        return CAM_RET_INIT_FAIL;
    }

    //configure sensors
    sensor_t * s = esp_camera_sensor_get();
    // initial sensors are flipped vertically and colors are a bit saturated
    if (s->id.PID == OV3660_PID) {
        s->set_vflip(s, 1); // flip it back
        s->set_brightness(s, 1); // up the brightness just a bit
        s->set_saturation(s, -2); // lower the saturation
    }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
    s->set_vflip(s, 1);
    s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
    s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
    setupLedFlash(LED_GPIO_NUM);
#endif

    switch (s->id.PID) {
    case (OV2640_PID):
        ESP_LOGI(CCAMERA_TAG, "start: Camera Type [OV2640]");
        break;
      
    case (OV3660_PID):
        ESP_LOGI(CCAMERA_TAG, "start: Camera Type [OV3660]");
        break;
      
    case (OV5640_PID):
        ESP_LOGI(CCAMERA_TAG, "start: Camera Type [OV5640]");
        break;
      
    default:
        ESP_LOGI(CCAMERA_TAG, "start: Camera Type [Other]");
        break;
    }
  
    //configure motion
    motion.setImageParameters(frameData[s->status.framesize].scaleFactor, frameData[s->status.framesize].sampleRate, frameData[s->status.framesize].frameWidth, frameData[s->status.framesize].frameHeight);
    motion.setDetectionParameters(3, 6, 15);

    //start tasks
    allowTasks = true;
    xSemaphoreTake(syncTaskSemaphore, 0);
    xTaskCreate(cameraMotionTask, "cameraMotionTask", TASK_STACK_SIZE, this, MOTI_TSK_PRIO, NULL);
    xSemaphoreTake(syncTaskSemaphore, portMAX_DELAY);
    xTaskCreate(cameraCaptureTask, "cameraCaptureTask", TASK_STACK_SIZE, this, CAPT_TSK_PRIO, NULL);
    xSemaphoreTake(syncTaskSemaphore, portMAX_DELAY);
    xTaskCreate(cameraTriggerTask, "cameraTriggerTask", TASK_STACK_SIZE, this, TRIG_TASK_PRIO, NULL);
    xSemaphoreTake(syncTaskSemaphore, portMAX_DELAY);

    ESP_LOGI(CCAMERA_TAG, "start: Complete");

  return CAM_RET_OK;
}

int CCamera::stop()
{
    if(allowTasks) {
        allowTasks = false;
        ESP_LOGD(CCAMERA_TAG, "stop: Stopping tasks");
        xSemaphoreTake(triggerTaskMutex, portMAX_DELAY);
        xSemaphoreGive(triggerTaskMutex);
        xSemaphoreTake(captureTaskMutex, portMAX_DELAY);
        xSemaphoreGive(captureTaskMutex);
        xSemaphoreTake(motionTaskMutex, portMAX_DELAY);
        xSemaphoreGive(motionTaskMutex);
        ESP_LOGI(CCAMERA_TAG, "stop: Tasks stopped");
    }

    if(esp_camera_deinit() == ESP_OK) {
        ESP_LOGI(CCAMERA_TAG, "stop: Camera shutdown");
    }
  
    return CAM_RET_OK;
}

int CCamera::startFile(const char* fileName, uint32_t maxFrameCount)
{
    sensor_t* s = esp_camera_sensor_get();
    return aviFile.startFile(fileName, frameData[s->status.framesize].frameWidth, frameData[s->status.framesize].frameHeight, frameData[s->status.framesize].defaultFPS, false, maxFrameCount);
}

int CCamera::closeFile()
{
    return aviFile.closeFile("");
}

bool CCamera::isRecording()
{
    return aviFile.isOpen();
}

bool CCamera::isRunning()
{
    return allowTasks;
}

bool CCamera::getAllowMotion()
{
    return allowMotion;
}

void CCamera::setAllowMotion(bool allow)
{
    allowMotion = allow;
}

void CCamera::setOnFrameCallback(bool (*cb)(camera_fb_t*))
{
    onFrame = cb;
}

void CCamera::cameraTriggerTask(void* vPtr)
{
    //subscribe to WDT
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    ESP_ERROR_CHECK(esp_task_wdt_status(NULL));

    CCamera* pCamera = (CCamera*)vPtr;
    xSemaphoreTake(pCamera->triggerTaskMutex, portMAX_DELAY);

    ESP_LOGI(CCAMERA_TAG, "Trigger Task: Started");
    xSemaphoreGive(pCamera->syncTaskSemaphore);
    sensor_t * s = esp_camera_sensor_get();
    uint32_t fps = frameData[s->status.framesize].defaultFPS ? frameData[s->status.framesize].defaultFPS : 1;
    uint32_t frameDelay = 1000 / fps;
    while(pCamera->allowTasks) {
        xSemaphoreGive(pCamera->triggerSemaphore);

        esp_task_wdt_reset();

        vTaskDelay(pdMS_TO_TICKS(frameDelay));
    }
  
    xSemaphoreGive(pCamera->triggerTaskMutex);

    //unsubscribe to WDT and deinit
    ESP_ERROR_CHECK(esp_task_wdt_delete(NULL));

    vTaskDelete(NULL);
}

void CCamera::cameraCaptureTask(void* vPtr)
{
    //subscribe to WDT
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    ESP_ERROR_CHECK(esp_task_wdt_status(NULL));

    CCamera* pCamera = (CCamera*)vPtr;
    xSemaphoreTake(pCamera->captureTaskMutex, portMAX_DELAY);

    ESP_LOGI(CCAMERA_TAG, "Capture Task: Started");
    xSemaphoreGive(pCamera->syncTaskSemaphore);
    while(pCamera->allowTasks) {
        if(xSemaphoreTake(pCamera->triggerSemaphore, pdMS_TO_TICKS(TIMEOUT_TASK)) == pdTRUE) {
            //are we waiting for a motion frame or are we recording?
            if (pCamera->aviFile.isOpen() || (!uxQueueMessagesWaiting(pCamera->motionQueue) && pCamera->allowMotion)) {
                // get camera frame
                camera_fb_t* fb = esp_camera_fb_get();
                if (fb) {
                    //check for motion
                    if (!pCamera->motion.getMotion()) {
                        if (pCamera->recordCountDown) {
                            pCamera->recordCountDown--;
                        }
                    }
                    else {
                        pCamera->recordCountDown = CAM_COUNT_DOWN;
                    }

                    //check if file is recording and if not should it be
                    if (pCamera->aviFile.isOpen()) {
                        if (!pCamera->recordCountDown || pCamera->aviFile.writeFrame(fb) != AVI_RET_OK) {
                            pCamera->aviFile.closeFile("");
                        }
                    }
                    else if (pCamera->recordCountDown) {
                        uint32_t currentTime = CurrentTime.ms();
                        uint32_t d = currentTime / 1000 / 60 / 60 / 24;
                        uint32_t h = (currentTime / 1000 / 60 / 60) % 24;
                        uint32_t m = (currentTime / 1000 / 60) % 60;
                        uint32_t s = (currentTime / 1000) % 60;

                        char fileName[256];
                        sprintf(fileName, "/sdcard/recording_%lu_%lu_%lu_%lu.avi", d, h, m, s);
                        pCamera->startFile(fileName, CAM_MAX_FRAMES);
                    }

                    //if the motion task is waiting feed it a new frame
                    if (!uxQueueMessagesWaiting(pCamera->motionQueue)) {
                        xQueueSend(pCamera->motionQueue, &fb, portMAX_DELAY);
                    }
                    else {
                        esp_camera_fb_return(fb);
                    }
                }
                else {
                    ESP_LOGW(CCAMERA_TAG, "Capture Task: fb is null");
                }
            }
            else {
                camera_fb_t* fb = esp_camera_fb_get();

                //send frame to callback and check if its keeping the frame
                if (pCamera->onFrame) {
                    if (!pCamera->onFrame(fb) && fb) {
                        esp_camera_fb_return(fb);
                    }
                }
                else if (fb) {
                    esp_camera_fb_return(fb);
                }
            }
        }

        esp_task_wdt_reset();

        vTaskDelay(pdMS_TO_TICKS(10));
    }
  
    xSemaphoreGive(pCamera->captureTaskMutex);

    //unsubscribe to WDT and deinit
    ESP_ERROR_CHECK(esp_task_wdt_delete(NULL));

    vTaskDelete(NULL);
}

void CCamera::cameraMotionTask(void* vPtr)
{
    //subscribe to WDT
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    ESP_ERROR_CHECK(esp_task_wdt_status(NULL));

    CCamera* pCamera = (CCamera*)vPtr;
    xSemaphoreTake(pCamera->motionTaskMutex, portMAX_DELAY);

    ESP_LOGI(CCAMERA_TAG, "Motion Task: Started");
    xSemaphoreGive(pCamera->syncTaskSemaphore);
    while(pCamera->allowTasks) {
        camera_fb_t* fb;
        if(xQueueReceive(pCamera->motionQueue, &fb, pdMS_TO_TICKS(TIMEOUT_TASK)) == pdTRUE) {
            if(fb) {
                pCamera->motion.checkMotion(fb);
                esp_camera_fb_return(fb);
            } else {
                ESP_LOGW(CCAMERA_TAG, "Motion Task: fb is null");
            }
        }

        esp_task_wdt_reset();

        vTaskDelay(pdMS_TO_TICKS(100));
    }
  
    xSemaphoreGive(pCamera->motionTaskMutex);

    //unsubscribe to WDT and deinit
    ESP_ERROR_CHECK(esp_task_wdt_delete(NULL));

    vTaskDelete(NULL);
}

void CCamera::setupLedFlash(int pin) 
{
#if CONFIG_LED_ILLUMINATOR_ENABLED
    ledcSetup(LED_LEDC_CHANNEL, 5000, 8);
    ledcAttachPin(pin, LED_LEDC_CHANNEL);
#else
    ESP_LOGI(CCAMERA_TAG, "LED flash is disabled -> CONFIG_LED_ILLUMINATOR_ENABLED = 0");
#endif
}
