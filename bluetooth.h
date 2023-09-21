#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <arduino.h>
#include "ringbuffer.h"

/*
 * DEFINES
 ****************************************************************************************
*/

#define MAX_GAP_LENGTH                14
#define DEFAULT_GAP_NAME              "BLE_TO_ISOTP20"

#define BP_GAP_KEY                    0xFF00FF00
#define BP_PASS_KEY                   0xFF00FF00

//BLE send queue size
#define BT_QUEUE_SIZE                 64
#define BT_STACK_SIZE                 3072
#define BT_TASK_PRIORITY              2

//BT max congestion
#define BT_CONGESTION_MAX             3000

#define spp_sprintf(s,...)            sprintf((char*)(s), ##__VA_ARGS__)
#define SPP_DATA_MAX_LEN              (512)
#define SPP_CMD_MAX_LEN               (20)
#define SPP_STATUS_MAX_LEN            (20)
#define SPP_DATA_BUFF_MAX_LEN         (2*1024)

//return types
#define BT_OK                         0
#define BT_QUEUE_FULL                 1
#define BT_ALLOC_ERROR                2
#define BT_NOTIFICATIONS_DISABLED     3
#define BT_INVALID_DATA               4
#define BT_WRITE_FAILURE              5
#define BT_INVALID_STATE              7
#define BT_INVALID_QUEUE              8

#define BT_DEINIT                     0
#define BT_INIT                       1
#define BT_LISTEN                     2
#define BT_RUN                        3

//Timeouts
#define TIMEOUT_SHORT                 50
#define TIMEOUT_NORMAL                100
#define TIMEOUT_LONG                  1000

///Attributes State Machine
enum {
    SPP_IDX_SVC,

    SPP_IDX_SPP_DATA_RECV_CHAR,
    SPP_IDX_SPP_DATA_RECV_VAL,

    SPP_IDX_SPP_DATA_NOTIFY_CHAR,
    SPP_IDX_SPP_DATA_NTY_VAL,
    SPP_IDX_SPP_DATA_NTF_CFG,

    SPP_IDX_SPP_COMMAND_CHAR,
    SPP_IDX_SPP_COMMAND_VAL,

    SPP_IDX_SPP_STATUS_CHAR,
    SPP_IDX_SPP_STATUS_VAL,
    SPP_IDX_SPP_STATUS_CFG,
    SPP_IDX_NB,
};

class CBluetoothCB {
  public:
    void (*dataReceived)(uint8_t* src, uint16_t size);
    void (*onConnect)();
    void (*onDisconnect)();
};

typedef struct send_packet {
  uint16_t size;
  uint8_t* buffer;
} send_packet_t;

typedef enum {
    NONE,
    BLE,
    CLASSIC
} BTConnectionType;

class CBluetooth {
  public:
    CBluetooth();
    ~CBluetooth();
  
    int               begin(CBluetoothCB callbacks);
    int               close();
  
    int               send(send_packet_t packet);
    int               currentState();
    bool              isConnected();
    BTConnectionType  getConnectionType();
    int               queueSpaces();
    int               queueWaiting();
    bool              setGapName(const char* name);
    bool              getGapName(const char* name);
    void              allowConnection(bool allow);
    
  private:
    static void       sendTask(void *pvParameters);
    uint16_t          clearQueue();
    
    uint16_t          spp_service_uuid;
    uint16_t          spp_data_receive_uuid;
    uint16_t          spp_data_notify_uuid;
    uint16_t          spp_status_uuid;

    QueueHandle_t     sppSendQueue;
    SemaphoreHandle_t congestedSemaphore;
    SemaphoreHandle_t taskMutex;
    SemaphoreHandle_t settingsMutex;

    BTConnectionType  connectionType;
    uint16_t          btState;
    CBluetoothCB      eventCallbacks;
    CRingBuffer*      ringBuffer;
};

extern CBluetooth Bluetooth;

#endif
