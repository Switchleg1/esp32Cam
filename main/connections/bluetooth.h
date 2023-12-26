#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#ifdef CONFIG_BT_ENABLED
#include "esp_gap_ble_api.h"
#include "esp_gap_bt_api.h"
#include "esp_gatts_api.h"
#include "esp_bt.h"
#include "esp_bt_defs.h"
#include "esp_bt_main.h"
#include "esp_spp_api.h"
#include "esp_bt_device.h"
#include "esp_mac.h"
#endif
#include "packet.h"
#include "globals.h"

/*
 * DEFINES
 ****************************************************************************************
*/

#define SPP_PROFILE_NUM                 1
#define SPP_PROFILE_APP_IDX             0
#define MAX_GAP_LENGTH                  14
#define DEFAULT_BLE_GAP_NAME            "MY_BLE_DEVICE"
#define DEFAULT_BT_GAP_NAME             "MY_BT_DEVICE"

#define BP_GAP_KEY                      0xFF00FF00
#define BP_PASS_KEY                     0xFF00FF00

//BLE send queue size
#define BT_QUEUE_SIZE                   8
#define BT_STACK_SIZE                   3072
#define BT_TASK_PRIORITY                2

//BT max congestion
#define BT_CONGESTION_MAX               3000

#define spp_sprintf(s,...)              sprintf((char*)(s), ##__VA_ARGS__)
#define SPP_DATA_MAX_LEN                (512)
#define SPP_CMD_MAX_LEN                 (20)
#define SPP_STATUS_MAX_LEN              (20)
#define SPP_DATA_BUFF_MAX_LEN           (2*1024)

//return types
#define BT_OK                           0
#define BT_QUEUE_FULL                   1
#define BT_ALLOC_ERROR                  2
#define BT_NOTIFICATIONS_DISABLED       3
#define BT_INVALID_DATA                 4
#define BT_WRITE_FAILURE                5
#define BT_INVALID_STATE                7
#define BT_INVALID_QUEUE                8

//Timeouts
#define TIMEOUT_SHORT                   50
#define TIMEOUT_NORMAL                  100
#define TIMEOUT_LONG                    1000

#define BT_MODE_NONE                    0
#define BT_MODE_BLE                     1
#define BT_MODE_BT                      2
#define BT_MODE_BOTH                    3

typedef enum {
  BT_DEINIT,
  BT_INIT,
  BT_LISTEN,
  BT_RUN
} BTState;

typedef enum {
    BT_NONE,
    BT_BLE,
    BT_CLASSIC
} BTConnectionType;

///Attributes State Machine
enum {
    SPP_IDX_SVC,

    SPP_IDX_SPP_DATA_RECV_CHAR,
    SPP_IDX_SPP_DATA_RECV_VAL,

    SPP_IDX_SPP_DATA_NOTIFY_CHAR,
    SPP_IDX_SPP_DATA_NTY_VAL,
    SPP_IDX_SPP_DATA_NTF_CFG,

    /*SPP_IDX_SPP_COMMAND_CHAR,
    SPP_IDX_SPP_COMMAND_VAL,

    SPP_IDX_SPP_STATUS_CHAR,
    SPP_IDX_SPP_STATUS_VAL,
    SPP_IDX_SPP_STATUS_CFG,*/
    SPP_IDX_NB,
};

class CBluetoothCB {
public:
    void (*dataReceived)(CPacket* packet);
    void (*onConnect)();
    void (*onDisconnect)();
};

#ifdef CONFIG_BT_ENABLED
struct gatts_profile_inst {
    esp_gatts_cb_t          gatts_cb;
    uint16_t                gatts_if;
    uint16_t                app_id;
    uint16_t                conn_id;
    uint16_t                service_handle;
    esp_gatt_srvc_id_t      service_id;
    uint16_t                char_handle;
    esp_bt_uuid_t           char_uuid;
    esp_gatt_perm_t         perm;
    esp_gatt_char_prop_t    property;
    uint16_t                descr_handle;
    esp_bt_uuid_t           descr_uuid;
};
#endif

typedef struct spp_receive_data_node{
    int32_t                       len;
    uint8_t*                      node_buff;
    struct spp_receive_data_node* next_node;
}spp_receive_data_node_t;

typedef struct spp_receive_data_buff{
    int32_t                     node_num;
    int32_t                     buff_size;
    spp_receive_data_node_t*    first_node;
}spp_receive_data_buff_t;

class CBluetooth {
  public:
    CBluetooth();
    ~CBluetooth();
  
    static int                  initBT();
    static int                  deinitBT();
    static int                  startModem(CBluetoothCB callbacks, uint8_t mode);
    static int                  stopModem();

    static int                  send(CPacket* packet);
    static BTState              currentState();
    static int                  currentMode();
    static bool                 isConnected();
    static BTConnectionType     getConnectionType();
    static int                  queueSpaces();
    static int                  queueWaiting();
    static bool                 setBLEGapName(const char* name);
    static bool                 getBLEGapName(char* name);
    static bool                 setBTGapName(const char* name);
    static bool                 getBTGapName(char* name);
    static void                 allowConnection(bool allow);
    static bool                 setBTPin(const char* pin, uint8_t length);
    static uint16_t             getMTU();
    static void                 setSendDelay(uint16_t delay);
    static uint16_t             getSendDelay();
    
  private:
#ifdef CONFIG_BT_ENABLED
    static void                 sendTask(void *pvParameters);
    static uint16_t             sendBle(CPacket* packet);
    static uint16_t             sendClassic(CPacket* packet);
    static uint16_t             startTask();
    static uint16_t             stopTask();
    static void                 bleGapEventHandler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);
    static void                 btGapEventHandler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param);
    static void                 bleGattsProfileEventHandler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
    static void                 bleGattsEventHandler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t* param);
    static void                 btSppEventHandler(esp_spp_cb_event_t event, esp_spp_cb_param_t* param);
    static char*                bda2str(uint8_t* bda, char* str, size_t size);
    static uint8_t              findCharAndDesrIndex(uint16_t handle);
    static void                 bleEnableNotification();
    static void                 bleDisableNotification();
    static bool                 bleStoreWRBuffer(esp_ble_gatts_cb_param_t* p_data);
    static void                 bleFreeWriteBuffer();
    static void                 bleSendBufferedMessage();
    static uint16_t             clearQueue();

    uint16_t                    sppHandleTable[SPP_IDX_NB];
    uint16_t                    sppServiceUUID;
    uint16_t                    sppDataReceiveUUID;
    uint16_t                    sppDataNotifyUUID;
    //uint16_t                    sppStatusUUID;
    //uint16_t                    sppCommandUUID;

    uint16_t                    primaryServiceUUID;
    uint16_t                    characterDeclarationUUID;
    uint16_t                    characterClientConfigUUID;
    uint8_t                     charPropReadNotify;
    uint8_t                     charPropReadWrite;

    esp_spp_sec_t               sppSecMask;
    esp_spp_role_t              sppRoleSlave;

    gatts_profile_inst          sppProfileTab[SPP_PROFILE_NUM];
    
    uint8_t                     sppDataReceiveVal[SPP_DATA_MAX_LEN];
    uint8_t                     sppDataNotifyVal[SPP_DATA_MAX_LEN];
    uint8_t                     sppDataNotifyCCC[sizeof(uint16_t)];

    //uint8_t                     sppCommandVal[SPP_CMD_MAX_LEN];
    //uint8_t                     sppStatusVal[SPP_STATUS_MAX_LEN];
    //uint8_t                     sppStatusCCC[sizeof(uint16_t)];

    esp_bt_pin_code_t           btPassword;
    uint8_t                     btPasswordLength;
    
    esp_ble_adv_params_t        sppAdvParams;
    spp_receive_data_buff_t     sppRecvDataBuff;
    spp_receive_data_node_t*    sppRecvLastNode;
    uint8_t                     sppAdvData[23];
    esp_gatts_attr_db_t         sppGattDB[SPP_IDX_NB];

    QueueHandle_t               sppSendQueue;
    SemaphoreHandle_t           congestedSemaphore;
    SemaphoreHandle_t           taskMutex;
    SemaphoreHandle_t           settingsMutex;

    char                        bleGapName[MAX_GAP_LENGTH+1];
    char                        btGapName[MAX_GAP_LENGTH+1];

    uint8_t                     btMode;
    uint32_t                    btClassicHandle;
    uint16_t                    bleSppConnId;
    esp_gatt_if_t               bleSppGattsIf;
    esp_bd_addr_t               sppRemoteBda;
    uint16_t                    bleSppMTUSize;
    BTConnectionType            connectionType;
    BTState                     btState;
    bool                        btAllowConnection;
    bool                        enable_data_ntf;
    CBluetoothCB                eventCallbacks;
    uint16_t                    sendDelay;
#endif
};

extern CBluetooth Bluetooth;

#endif
