#include <string.h>
#include "esp_heap_caps.h"
#include "globals.h"
#include "communications.h"
#include "communications_command.h"
#include "communications_command_delete_file.h"
#include "communications_command_send_file.h"
#include "communications_command_directory.h"
#include "communications_command_camera.h"
#include "communications_command_ota.h"

#define MAIN_TAG "Main"

#define TASK_TICK_TIME      5
#define TASK_DELAY_TIME(x)  (x / TASK_TICK_TIME)

extern "C" void app_main(void)
{
    //subscribe to WDT
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    ESP_ERROR_CHECK(esp_task_wdt_status(NULL));

    ESP_LOGI(MAIN_TAG, "Starting");

    //start nvs
    ESP_ERROR_CHECK(nvs_flash_init());

    //start sd card
    while (Fat32.mount() != FAT_RET_OK) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_task_wdt_reset();
    }
    Fat32.listDir("/sdcard", 2);

    //setup communications
    CComsCommandDirectory*  pCommandDirectory   = new CComsCommandDirectory( 0x01, 3000);
    CComsCommandSendFile*   pCommandSendFile    = new CComsCommandSendFile(  0x02, 3000);
    CComsCommandDeleteFile* pCommandDeleteFile  = new CComsCommandDeleteFile(0x03, 3000);
    CComsCommandCamera*     pCommandCamera      = new CComsCommandCamera(    0x04, 3000);
    CComsCommandOTA*        pCommandOTA         = new CComsCommandOTA(       0xA0, 3000);
    Communications.initComs();
    Communications.addCommand(pCommandDirectory);
    Communications.addCommand(pCommandSendFile);
    Communications.addCommand(pCommandDeleteFile);
    Communications.addCommand(pCommandCamera);
    Communications.addCommand(pCommandOTA);
    Communications.startComs();
    
    uint16_t taskTickCount = 0;
    while (1) {
        if (!Camera.isRunning()) {
            Camera.start();
        }

        Camera.setAllowMotion(!Communications.isConnected());
    
        if (!(taskTickCount % 10000)) {
            ESP_LOGI(MAIN_TAG, "Free heap: [%d] RAM: [%d] PSRAM: [%d]", heap_caps_get_free_size(MALLOC_CAP_8BIT), 
                                                                        heap_caps_get_free_size(MALLOC_CAP_8BIT) - heap_caps_get_free_size(MALLOC_CAP_SPIRAM), 
                                                                        heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }

        taskTickCount++;
        
        vTaskDelay(pdMS_TO_TICKS(TASK_TICK_TIME));
        esp_task_wdt_reset();
    }

    //unsubscribe to WDT and deinit
    ESP_ERROR_CHECK(esp_task_wdt_delete(NULL));
}
