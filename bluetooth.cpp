#include "esp_gap_ble_api.h"
#include "esp_gap_bt_api.h"
#include "esp_gatts_api.h"
#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_spp_api.h"
#include "esp_bt_device.h"
#include "esp_mac.h"
#include "bluetooth.h"

#define SPP_PROFILE_NUM           1
#define SPP_PROFILE_APP_IDX       0
#define ESP_SPP_APP_ID            0x56
#define SPP_SVC_INST_ID           0
#define DEFAULT_MTU_SIZE          23

/// Characteristic UUID
#define ESP_GATT_UUID_SPP_SERVICE           0xABF0
#define ESP_GATT_UUID_SPP_DATA_RECEIVE      0xABF1
#define ESP_GATT_UUID_SPP_DATA_NOTIFY       0xABF2
#define ESP_GATT_UUID_SPP_COMMAND_RECEIVE   0xABF3
#define ESP_GATT_UUID_SPP_COMMAND_NOTIFY    0xABF4

CBluetooth Bluetooth;

CBluetooth::CBluetooth()
{
  spp_service_uuid      = ESP_GATT_UUID_SPP_SERVICE;
  spp_data_receive_uuid = ESP_GATT_UUID_SPP_DATA_RECEIVE;
  spp_data_notify_uuid  = ESP_GATT_UUID_SPP_DATA_NOTIFY;
  spp_status_uuid       = ESP_GATT_UUID_SPP_COMMAND_NOTIFY;

  ringBuffer          = new CRingBuffer(8192);
  taskMutex           = xSemaphoreCreateMutex();
  settingsMutex       = xSemaphoreCreateMutex();
  congestedSemaphore  = xSemaphoreCreateBinary();
  sppSendQueue        = xQueueCreate(BT_QUEUE_SIZE, sizeof(send_packet_t));
}

CBluetooth::~CBluetooth()
{
  if(ringBuffer) {
    delete ringBuffer;
  }

  if(taskMutex) {
    vSemaphoreDelete(taskMutex);
  }

  if(settingsMutex) {
    vSemaphoreDelete(settingsMutex);
  }

  if(congestedSemaphore) {
    vSemaphoreDelete(congestedSemaphore);
  }
}
  
int CBluetooth::begin(CBluetoothCB callbacks)
{
  memcpy(&eventCallbacks, &callbacks, sizeof(CBluetoothCB));

  xTaskCreate(CBluetooth::sendTask, "CBluetooth::sendTask", BT_STACK_SIZE, this, BT_TASK_PRIORITY, NULL);
  
  return BT_OK;
}

int CBluetooth::close()
{
  return BT_OK;
}
  
int CBluetooth::send(send_packet_t packet)
{
  return BT_OK;
}

int CBluetooth::currentState()
{
  return BT_OK;
}

bool CBluetooth::isConnected()
{
  return false;
}

BTConnectionType CBluetooth::getConnectionType()
{
  return NONE;
}

int CBluetooth::queueSpaces()
{
  return 0;
}

int CBluetooth::queueWaiting()
{
  return 0;
}

bool CBluetooth::setGapName(const char* name)
{
  return false;
}

bool CBluetooth::getGapName(const char* name)
{
  return false;
}

void CBluetooth::allowConnection(bool allow)
{
  
}

void CBluetooth::sendTask(void *pvParameters)
{
  CBluetooth* bt = (CBluetooth*)pvParameters;
  
  xSemaphoreTake(bt->taskMutex, portMAX_DELAY);
  bt->clearQueue();

  send_packet_t packet;
  while(bt->btState == BT_RUN) {
    ESP_LOGD(BT_TAG, "bt_send_task - memory free [%d]", uxTaskGetStackHighWaterMark(NULL));
    if (xQueueReceive(bt->sppSendQueue, &packet, pdMS_TO_TICKS(TIMEOUT_LONG)) == pdTRUE) {
        //check to see if this is a valid message and we can send messages
        if(bt->btState != BT_RUN) {
            Serial.println("bt_send_task - unable to send message, task is shutting down.");
            free(packet.buffer);
            continue;
        }

        if (!packet.buffer) {
            Serial.println("bt_send_task - unable to send message, buffer is null.");
            continue;
        }

        if(!packet.size) {
            Serial.println("bt_send_task - message length is 0, message deleted");
            free(packet.buffer);
            continue;
        }

        //Connected and notifications are enabled check for congestion
        xSemaphoreTake(bt->congestedSemaphore, pdMS_TO_TICKS(BT_CONGESTION_MAX));
        xSemaphoreGive(bt->congestedSemaphore);

        Serial.printf("bt_send_task - sending message [%d]\n", packet.size);

        switch (bt->connectionType) {
        case BLE:
            //bt_send_ble(packet);
            break;
        case CLASSIC:
            //bt_send_classic(packet);
            break;
        default:
            if (packet.buffer) {
                free(packet.buffer);
            }
        }
    }

    taskYIELD();
  }

  bt->clearQueue();
  xSemaphoreGive(bt->taskMutex);

  vTaskDelete(NULL);
}

uint16_t CBluetooth::clearQueue()
{
    if (!sppSendQueue) {
        return BT_INVALID_QUEUE;
    }

    send_packet_t packet;
    while (xQueueReceive(sppSendQueue, &packet, 0) == pdTRUE) {
        if (packet.buffer) {
            free(packet.buffer);
        }
    }

    return BT_OK;
}
