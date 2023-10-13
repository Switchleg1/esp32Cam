#include <stdio.h>
#include <string.h>
#include "bluetooth.h"

#define SPP_PROFILE_NUM                     1
#define SPP_PROFILE_APP_IDX                 0
#define ESP_SPP_APP_ID                      0x56
#define SPP_SVC_INST_ID                     0
#define DEFAULT_MTU_SIZE                    23
#define BT_DEFAULT_SEND_DELAY               1

/// Characteristic UUID
#define ESP_GATT_UUID_SPP_SERVICE           0xABF0
#define ESP_GATT_UUID_SPP_DATA_RECEIVE      0xABF1
#define ESP_GATT_UUID_SPP_DATA_NOTIFY       0xABF2
//#define ESP_GATT_UUID_SPP_COMMAND_RECEIVE   0xABF3
//#define ESP_GATT_UUID_SPP_COMMAND_NOTIFY    0xABF4

#define CHAR_DECLARATION_SIZE               (sizeof(uint8_t))

#define CBT_TAG                             "CBluetooth"

CBluetooth Bluetooth;

CBluetooth::CBluetooth()
{
#ifdef CONFIG_BT_ENABLED
    btState                 = BT_DEINIT;
    taskMutex               = NULL;
    settingsMutex           = NULL;
    congestedSemaphore      = NULL;
    sppSendQueue            = NULL;

    bleSppMTUSize           = DEFAULT_MTU_SIZE;
    bleSppConnId            = 0xffff;
    bleSppGattsIf           = 0xff;

    btAllowConnection       = false;
    enable_data_ntf         = false;
    connectionType          = BT_NONE;

    btPasswordLength        = 0;
    memset(btPassword, 0, sizeof(esp_bt_pin_code_t));
  
    memset(&eventCallbacks, 0, sizeof(CBluetoothCB));
  
    if(this != &Bluetooth) {
        ESP_LOGE(CBT_TAG, "CBluetooth(): Cannot create more than one instance of CBluetooth()");
        return;
    }
#endif
}

CBluetooth::~CBluetooth()
{
#ifdef CONFIG_BT_ENABLED
    stopModem();
    deinitBT();
#endif
}

int CBluetooth::initBT()
{
#ifdef CONFIG_BT_ENABLED
    if (Bluetooth.btState != BT_DEINIT) {
        ESP_LOGD(CBT_TAG, "initBT: invalid state");

        return BT_INVALID_STATE;
    }

    Bluetooth.taskMutex                         = xSemaphoreCreateMutex();
    Bluetooth.settingsMutex                     = xSemaphoreCreateMutex();
    Bluetooth.congestedSemaphore                = xSemaphoreCreateBinary();
    Bluetooth.sppSendQueue                      = xQueueCreate(BT_QUEUE_SIZE, sizeof(CPacket*));

    Bluetooth.sppAdvParams.adv_int_min          = 0x20;
    Bluetooth.sppAdvParams.adv_int_max          = 0x40;
    Bluetooth.sppAdvParams.adv_type             = ADV_TYPE_IND;
    Bluetooth.sppAdvParams.own_addr_type        = BLE_ADDR_TYPE_PUBLIC;
    Bluetooth.sppAdvParams.channel_map          = ADV_CHNL_ALL;
    Bluetooth.sppAdvParams.adv_filter_policy    = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

    Bluetooth.sppRecvDataBuff.node_num          = 0;
    Bluetooth.sppRecvDataBuff.buff_size         = 0;
    Bluetooth.sppRecvDataBuff.first_node        = NULL;
    Bluetooth.sppRecvLastNode                   = NULL;

    Bluetooth.sppServiceUUID                    = ESP_GATT_UUID_SPP_SERVICE;
    Bluetooth.sppDataReceiveUUID                = ESP_GATT_UUID_SPP_DATA_RECEIVE;
    Bluetooth.sppDataNotifyUUID                 = ESP_GATT_UUID_SPP_DATA_NOTIFY;
    //Bluetooth.sppCommandUUID                    = ESP_GATT_UUID_SPP_COMMAND_RECEIVE;
    //Bluetooth.sppStatusUUID                     = ESP_GATT_UUID_SPP_COMMAND_NOTIFY;

    Bluetooth.primaryServiceUUID                = ESP_GATT_UUID_PRI_SERVICE;
    Bluetooth.characterDeclarationUUID          = ESP_GATT_UUID_CHAR_DECLARE;
    Bluetooth.characterClientConfigUUID         = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
    Bluetooth.charPropReadNotify                = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY; // | ESP_GATT_CHAR_PROP_BIT_INDICATE;
    Bluetooth.charPropReadWrite                 = ESP_GATT_CHAR_PROP_BIT_WRITE_NR | ESP_GATT_CHAR_PROP_BIT_READ;

    Bluetooth.sppSecMask                        = ESP_SPP_SEC_AUTHENTICATE;
    Bluetooth.sppRoleSlave                      = ESP_SPP_ROLE_SLAVE;

    memset(Bluetooth.sppHandleTable, 0, sizeof(Bluetooth.sppHandleTable));
    memset(Bluetooth.sppRemoteBda, 0, sizeof(Bluetooth.sppRemoteBda));

    memset(Bluetooth.sppDataNotifyVal, 0, sizeof(Bluetooth.sppDataNotifyVal));
    memset(Bluetooth.sppDataNotifyCCC, 0, sizeof(Bluetooth.sppDataNotifyCCC));
    memset(Bluetooth.sppDataReceiveVal, 0, sizeof(Bluetooth.sppDataReceiveVal));

    //memset(Bluetooth.sppCommandVal, 0, sizeof(Bluetooth.sppCommandVal));
    //memset(Bluetooth.sppStatusVal, 0, sizeof(Bluetooth.sppStatusVal));
    //memset(Bluetooth.sppStatusCCC, 0, sizeof(Bluetooth.sppStatusCCC));

    uint8_t spp_adv_temp[23] = {
        /* Flags */
        0x02,0x01,0x06,
        /* Complete List of 16-bit Service Class UUIDs */
        0x03,0x03,0xF0,0xAB,
        /* Complete Local Name in advertising */
        0x0F,0x09, 'M', 'Y', '_', 'B', 'L', 'E', '_', 'D', 'E', 'V', 'I','C', 'E', '1'
    };
    memcpy(Bluetooth.sppAdvData, spp_adv_temp, 23);

    setBLEGapName(DEFAULT_BLE_GAP_NAME);
    setBTGapName(DEFAULT_BT_GAP_NAME);

    esp_gatts_attr_db_t sppGattDBTemp[SPP_IDX_NB] =
    {
        //SPP -  Service Declaration
        [SPP_IDX_SVC] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&Bluetooth.primaryServiceUUID, ESP_GATT_PERM_READ,
        sizeof(Bluetooth.sppServiceUUID), sizeof(Bluetooth.sppServiceUUID), (uint8_t*)&Bluetooth.sppServiceUUID}},

        //SPP -  data receive characteristic Declaration
        [SPP_IDX_SPP_DATA_RECV_CHAR] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&Bluetooth.characterDeclarationUUID, ESP_GATT_PERM_READ,
        CHAR_DECLARATION_SIZE,CHAR_DECLARATION_SIZE, (uint8_t*)&Bluetooth.charPropReadWrite}},

        //SPP -  data receive characteristic Value
        [SPP_IDX_SPP_DATA_RECV_VAL] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&Bluetooth.sppDataReceiveUUID, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
        SPP_DATA_MAX_LEN,sizeof(sppDataReceiveVal), (uint8_t*)Bluetooth.sppDataReceiveVal}},

        //SPP -  data notify characteristic Declaration
        [SPP_IDX_SPP_DATA_NOTIFY_CHAR] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&Bluetooth.characterDeclarationUUID, ESP_GATT_PERM_READ,
        CHAR_DECLARATION_SIZE,CHAR_DECLARATION_SIZE, (uint8_t*)&Bluetooth.charPropReadNotify}},

        //SPP -  data notify characteristic Value
        [SPP_IDX_SPP_DATA_NTY_VAL] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&Bluetooth.sppDataNotifyUUID, ESP_GATT_PERM_READ,
        SPP_DATA_MAX_LEN, sizeof(Bluetooth.sppDataNotifyVal), (uint8_t*)Bluetooth.sppDataNotifyVal}},

        //SPP -  data notify characteristic - Client Characteristic Configuration Descriptor
        [SPP_IDX_SPP_DATA_NTF_CFG] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&Bluetooth.characterClientConfigUUID, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
        sizeof(uint16_t),sizeof(Bluetooth.sppDataNotifyCCC), (uint8_t*)Bluetooth.sppDataNotifyCCC}},

        //SPP -  command characteristic Declaration
        /*[SPP_IDX_SPP_COMMAND_CHAR] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&Bluetooth.characterDeclarationUUID, ESP_GATT_PERM_READ,
        CHAR_DECLARATION_SIZE,CHAR_DECLARATION_SIZE, (uint8_t*)&Bluetooth.charPropReadWrite}},

        //SPP -  command characteristic Value
        [SPP_IDX_SPP_COMMAND_VAL] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&Bluetooth.sppCommandUUID, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
        SPP_CMD_MAX_LEN,sizeof(Bluetooth.sppCommandVal), (uint8_t*)Bluetooth.sppCommandVal}},

        //SPP -  status characteristic Declaration
        [SPP_IDX_SPP_STATUS_CHAR] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&Bluetooth.characterDeclarationUUID, ESP_GATT_PERM_READ,
        CHAR_DECLARATION_SIZE,CHAR_DECLARATION_SIZE, (uint8_t*)&Bluetooth.charPropReadNotify}},

        //SPP -  status characteristic Value
        [SPP_IDX_SPP_STATUS_VAL] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&Bluetooth.sppStatusUUID, ESP_GATT_PERM_READ,
        SPP_STATUS_MAX_LEN,sizeof(Bluetooth.sppStatusVal), (uint8_t*)Bluetooth.sppStatusVal}},

        //SPP -  status characteristic - Client Characteristic Configuration Descriptor
        [SPP_IDX_SPP_STATUS_CFG] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&Bluetooth.characterClientConfigUUID, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
        sizeof(uint16_t),sizeof(Bluetooth.sppStatusCCC), (uint8_t*)Bluetooth.sppStatusCCC}},*/
    };
    memcpy(Bluetooth.sppGattDB, sppGattDBTemp, sizeof(esp_gatts_attr_db_t) * SPP_IDX_NB);

    for (uint8_t i = 0; i < SPP_PROFILE_NUM; i++) {
        Bluetooth.sppProfileTab[i].gatts_cb = bleGattsProfileEventHandler;
        Bluetooth.sppProfileTab[i].gatts_if = ESP_GATT_IF_NONE;       /* Not get the gatt_if, so initial is ESP_GATT_IF_NONE */
    }

    Bluetooth.sendDelay         = BT_DEFAULT_SEND_DELAY;

    Bluetooth.connectionType    = BT_NONE;
    Bluetooth.btState           = BT_INIT;

    return BT_OK;

#else

    return BT_INVALID_STATE;
#endif
}

int CBluetooth::deinitBT()
{
#ifdef CONFIG_BT_ENABLED
    if (Bluetooth.btState != BT_INIT) {
        ESP_LOGD(CBT_TAG, "deinitBT: invalid state");

        return BT_INVALID_STATE;
    }

    bool didDeInit = false;
    if (Bluetooth.sppSendQueue) {
        clearQueue();
        vQueueDelete(Bluetooth.sppSendQueue);
        Bluetooth.sppSendQueue  = NULL;
        didDeInit               = true;
    }

    if (Bluetooth.taskMutex) {
        vSemaphoreDelete(Bluetooth.taskMutex);
        Bluetooth.taskMutex = NULL;
        didDeInit           = true;
    }

    if (Bluetooth.settingsMutex) {
        vSemaphoreDelete(Bluetooth.settingsMutex);
        Bluetooth.taskMutex = NULL;
        didDeInit           = true;
    }

    if (Bluetooth.congestedSemaphore) {
        vSemaphoreDelete(Bluetooth.congestedSemaphore);
        Bluetooth.taskMutex = NULL;
        didDeInit           = true;
    }

    Bluetooth.connectionType    = BT_NONE;
    Bluetooth.btState           = BT_DEINIT;

    if (didDeInit) {
        ESP_LOGI(CBT_TAG, "deinitBT: success");

        return BT_OK;
    }

    ESP_LOGI(CBT_TAG, "deinitBT: error");

#endif
    return BT_INVALID_STATE;
}
  
int CBluetooth::startModem(CBluetoothCB callbacks, uint8_t mode)
{
#ifdef CONFIG_BT_ENABLED
    if (Bluetooth.btState != BT_INIT) {
        ESP_LOGD(CBT_TAG, "startModem: invalid state");

        return BT_INVALID_STATE;
    }

    //copy callbacks
    memcpy(&Bluetooth.eventCallbacks, &callbacks, sizeof(CBluetoothCB));

    //initialize bt
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    if ((mode & BT_MODE_BLE) && (mode & BT_MODE_BT)) {
        ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BTDM));
    }
    else if (mode & ESP_BT_MODE_BLE) {
        ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));
    }
    else if (mode & BT_MODE_BT) {
        ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    }
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
  
    if(mode & BT_MODE_BLE) {
        ESP_ERROR_CHECK(esp_ble_gatts_register_callback(Bluetooth.bleGattsEventHandler));
        ESP_ERROR_CHECK(esp_ble_gap_register_callback(Bluetooth.bleGapEventHandler));
        ESP_ERROR_CHECK(esp_ble_gatts_app_register(ESP_SPP_APP_ID));
    }

    if(mode & BT_MODE_BT) {
        ESP_ERROR_CHECK(esp_bt_gap_register_callback(Bluetooth.btGapEventHandler));
        ESP_ERROR_CHECK(esp_spp_register_callback(Bluetooth.btSppEventHandler));

        esp_spp_cfg_t spp_bt_config = {
            ESP_SPP_MODE_CB,
            true,
            0
        };
        ESP_ERROR_CHECK(esp_spp_enhanced_init(&spp_bt_config));
   
        esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE; //ESP_BT_PIN_TYPE_FIXED;
        esp_bt_gap_set_pin(pin_type, Bluetooth.btPasswordLength, Bluetooth.btPassword);
    }

    //allow connections by default
    Bluetooth.btAllowConnection = true;
    Bluetooth.connectionType    = BT_NONE;
    Bluetooth.btState           = BT_LISTEN;
    Bluetooth.btMode            = mode;

    ESP_LOGI(CBT_TAG, "begin: Complete");
  
    return BT_OK;
#else
    return BT_INVALID_STATE;
#endif
}

int CBluetooth::stopModem()
{
#ifdef CONFIG_BT_ENABLED
    if (Bluetooth.btState < BT_LISTEN) {
        ESP_LOGD(CBT_TAG, "stopModem: invalid state");

        return BT_INVALID_STATE;
    }

    bool bStopModem = false;
  
    Bluetooth.stopTask();
    Bluetooth.clearQueue();
    Bluetooth.bleFreeWriteBuffer();

    //shutdown BLE
    if (esp_bluedroid_disable() == ESP_OK) {
        bStopModem = true;
    }

    if (esp_bluedroid_deinit() == ESP_OK) {
        bStopModem = true;
    }

    if (esp_bt_controller_disable() == ESP_OK) {
        bStopModem = true;
    }

    if (esp_bt_controller_deinit() == ESP_OK) {
        bStopModem = true;
    }

    Bluetooth.connectionType    = BT_NONE;
    Bluetooth.btState           = BT_INIT;

    if (bStopModem) {
        ESP_LOGI(CBT_TAG, "stopModem: Complete");
    
        return BT_OK;
    }

#endif
    return BT_INVALID_STATE;
}
  
int CBluetooth::send(CPacket* packet)
{
#ifdef CONFIG_BT_ENABLED
    if (Bluetooth.connectionType == BT_NONE) {
        ESP_LOGW(CBT_TAG, "send: not connected");
        return BT_INVALID_STATE;
    }

    if(!Bluetooth.sppSendQueue || xQueueSend(Bluetooth.sppSendQueue, &packet, pdMS_TO_TICKS(TIMEOUT_NORMAL)) != pdTRUE) {
        ESP_LOGE(CBT_TAG, "send: queue is full");
        return BT_QUEUE_FULL;
    }
    
    return BT_OK;
#else
    return BT_INVALID_STATE;
#endif
}

BTState CBluetooth::currentState()
{
#ifdef CONFIG_BT_ENABLED
    return Bluetooth.btState;
#else
    return BT_DEINIT;
#endif
}

int CBluetooth::currentMode()
{
#ifdef CONFIG_BT_ENABLED
    return Bluetooth.btMode;
#else
    return BT_MODE_NONE;
#endif
}

bool CBluetooth::isConnected()
{
#ifdef CONFIG_BT_ENABLED
    return Bluetooth.btState == BT_RUN;
#else
    return false;
#endif
}

BTConnectionType CBluetooth::getConnectionType()
{
#ifdef CONFIG_BT_ENABLED
    return Bluetooth.connectionType;
#else
    return BT_NONE;
#endif
}

int CBluetooth::queueSpaces()
{
#ifdef CONFIG_BT_ENABLED
    return uxQueueSpacesAvailable(Bluetooth.sppSendQueue);
#else
    return 0;
#endif
}

int CBluetooth::queueWaiting()
{
#ifdef CONFIG_BT_ENABLED
    return uxQueueMessagesWaiting(Bluetooth.sppSendQueue);
#else
    return 0;
#endif
}

bool CBluetooth::setBLEGapName(const char* name)
{
#ifdef CONFIG_BT_ENABLED
    if(name) {
        uint8_t len = strlen(name);
        if(len <= MAX_GAP_LENGTH) {
            xSemaphoreTake(Bluetooth.settingsMutex, portMAX_DELAY);
            memset((char*)&Bluetooth.sppAdvData[9], 0, MAX_GAP_LENGTH);
            memcpy((char*)&Bluetooth.sppAdvData[9], name, len);
            Bluetooth.sppAdvData[7] = len+1;
            strcpy(Bluetooth.bleGapName, name);

            if(Bluetooth.btState == BT_LISTEN) {
                ESP_ERROR_CHECK(esp_ble_gap_set_device_name(Bluetooth.bleGapName));
            }

            ESP_LOGI(CBT_TAG, "setBLEGapName: set GAP name [%s]", Bluetooth.bleGapName);
            xSemaphoreGive(Bluetooth.settingsMutex);
      
            return true;
        }
    }

    ESP_LOGI(CBT_TAG, "setBLEGapName: unable to set GAP name");

#endif
  
    return false;
}

bool CBluetooth::getBLEGapName(char* name)
{
#ifdef CONFIG_BT_ENABLED
    if(name) {
        xSemaphoreTake(Bluetooth.settingsMutex, portMAX_DELAY);
            strcpy(name, Bluetooth.bleGapName);
        xSemaphoreGive(Bluetooth.settingsMutex);

        return true;
    }

#endif
  
    return false;
}

bool CBluetooth::setBTGapName(const char* name)
{
#ifdef CONFIG_BT_ENABLED
    if(name) {
        uint8_t len = strlen(name);
        if(len <= MAX_GAP_LENGTH) {
            xSemaphoreTake(Bluetooth.settingsMutex, portMAX_DELAY);
            strcpy(Bluetooth.btGapName, name);

            ESP_LOGI(CBT_TAG, "setBTGapName: set GAP name [%s]", Bluetooth.btGapName);
            xSemaphoreGive(Bluetooth.settingsMutex);
      
            return true;
        }
    }

    ESP_LOGI(CBT_TAG, "setBTGapName: unable to set GAP name");

#endif
  
    return false;
}

bool CBluetooth::getBTGapName(char* name)
{
#ifdef CONFIG_BT_ENABLED
    if(name) {
        xSemaphoreTake(Bluetooth.settingsMutex, portMAX_DELAY);
            strcpy(name, Bluetooth.btGapName);
        xSemaphoreGive(Bluetooth.settingsMutex);

        return true;
    }

#endif
  
    return false;
}

void CBluetooth::allowConnection(bool allow)
{
#ifdef CONFIG_BT_ENABLED
    Bluetooth.btAllowConnection = allow;
#endif
}

bool CBluetooth::setBTPin(const char* pin, uint8_t length)
{
#ifdef CONFIG_BT_ENABLED
    if(pin && length <= sizeof(esp_bt_pin_code_t)) {
        if(Bluetooth.settingsMutex) xSemaphoreTake(Bluetooth.settingsMutex, portMAX_DELAY);
            memset(&Bluetooth.btPassword, 0, sizeof(esp_bt_pin_code_t));
            memcpy(&Bluetooth.btPassword, pin, length);
            Bluetooth.btPasswordLength = length;

            if(Bluetooth.btState >= BT_LISTEN) {
                esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_FIXED;
                esp_bt_gap_set_pin(pin_type, Bluetooth.btPasswordLength, Bluetooth.btPassword);
            }
      
            ESP_LOGI(CBT_TAG, "setBTPin: set Pin [%s]", Bluetooth.btPassword);
        if (Bluetooth.settingsMutex) xSemaphoreGive(Bluetooth.settingsMutex);

        return true;
    }

#endif

    return false;
}

uint16_t CBluetooth::getMTU()
{
#ifdef CONFIG_BT_ENABLED
    return Bluetooth.bleSppMTUSize;
#else
    return 0;
#endif
}

void CBluetooth::setSendDelay(uint16_t delay)
{
#ifdef CONFIG_BT_ENABLED
    Bluetooth.sendDelay = delay;
#endif
}

uint16_t CBluetooth::getSendDelay()
{
#ifdef CONFIG_BT_ENABLED
    return Bluetooth.sendDelay;
#else
    return 0;
#endif
}

#ifdef CONFIG_BT_ENABLED

void CBluetooth::sendTask(void *pvParameters)
{
    //subscribe to WDT
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    ESP_ERROR_CHECK(esp_task_wdt_status(NULL));

    xSemaphoreTake(Bluetooth.taskMutex, portMAX_DELAY);
    Bluetooth.clearQueue();

    CPacket* packet;
    while(Bluetooth.btState == BT_RUN) {
        ESP_LOGD(CBT_TAG, "sendTask: memory free [%d]", uxTaskGetStackHighWaterMark(NULL));
        if (xQueueReceive(Bluetooth.sppSendQueue, &packet, pdMS_TO_TICKS(TIMEOUT_LONG)) == pdTRUE) {
            //check to see if this is a valid message and we can send messages
            if(Bluetooth.btState != BT_RUN) {
                ESP_LOGW(CBT_TAG, "sendTask: unable to send message, task is shutting down.");
                delete packet;
                continue;
            }

            if (!packet || !packet->valid()) {
                ESP_LOGW(CBT_TAG, "sendTask: unable to send message, buffer is null.");
                delete packet;
                continue;
            }

            //Connected and notifications are enabled check for congestion
            xSemaphoreTake(Bluetooth.congestedSemaphore, pdMS_TO_TICKS(BT_CONGESTION_MAX));
            xSemaphoreGive(Bluetooth.congestedSemaphore);

            ESP_LOGD(CBT_TAG, "sendTask: sending message [%d]", packet->size);

            switch (Bluetooth.connectionType) {
            case BT_BLE:
                Bluetooth.sendBle(packet);
                break;
          
            case BT_CLASSIC:
                Bluetooth.sendClassic(packet);
                break;
          
            default:
                delete packet;
            }
        }

        esp_task_wdt_reset();

        vTaskDelay(pdMS_TO_TICKS(Bluetooth.sendDelay));
    }

    Bluetooth.clearQueue();
    xSemaphoreGive(Bluetooth.taskMutex);

    //unsubscribe to WDT and deinit
    ESP_ERROR_CHECK(esp_task_wdt_delete(NULL));

    vTaskDelete(NULL);
}

uint16_t CBluetooth::sendBle(CPacket* packet)
{
    if (!packet || !packet->valid()) {
        ESP_LOGD(CBT_TAG, "sendBle: invalid packet");
    
        return BT_INVALID_DATA;
    }
    
    if(!Bluetooth.enable_data_ntf) {
        ESP_LOGW(CBT_TAG, "sendBle: notifications not enabled, message deleted");
        free(packet);

        return BT_NOTIFICATIONS_DISABLED;
    }

    ESP_LOGD(CBT_TAG, "sendBle: packet size [%u]", packet->size);

    uint16_t retValue       = BT_OK;
    uint8_t* dataPosition   = packet->data;
    int32_t packetLeft      = packet->size;
    while(packetLeft) {
        uint16_t maxLength  = Bluetooth.bleSppMTUSize - 3;
        uint16_t sendLength = packetLeft;
        if (sendLength > maxLength) {
            sendLength = maxLength;
        }

        ESP_LOGD(CBT_TAG, "sendBle:  sending [%u]", sendLength);
        uint8_t timeout = 0;
        while (esp_ble_gatts_send_indicate(Bluetooth.bleSppGattsIf, Bluetooth.bleSppConnId, Bluetooth.sppHandleTable[SPP_IDX_SPP_DATA_NTY_VAL], sendLength, dataPosition, false) != ESP_OK && timeout++ < 10) {
            ESP_LOGW(CBT_TAG, "sendBle: write failure");
            retValue = BT_WRITE_FAILURE;
            vTaskDelay(pdMS_TO_TICKS(Bluetooth.sendDelay));
        }

        dataPosition    += sendLength;
        packetLeft      -= sendLength;

        vTaskDelay(pdMS_TO_TICKS(Bluetooth.sendDelay));
    }
    delete packet;

    return retValue;
}

uint16_t CBluetooth::sendClassic(CPacket* packet)
{
    if (!packet || !packet->valid()) {
        ESP_LOGD(CBT_TAG, "sendClassic: invalid packet");
    
        return BT_INVALID_DATA;
    }
  
    ESP_LOGD(CBT_TAG, "sendClassic: sending packet");

    uint16_t retValue = BT_OK;
    if (esp_spp_write(Bluetooth.btClassicHandle, packet->size, packet->data) != ESP_OK) {
        ESP_LOGW(CBT_TAG, "sendClassic: write failure");
        retValue = BT_WRITE_FAILURE;
        vTaskDelay(pdMS_TO_TICKS(Bluetooth.sendDelay));
    }
    delete packet;

    return retValue;
}

uint16_t CBluetooth::startTask()
{
    if (Bluetooth.btState != BT_LISTEN || xSemaphoreTake(Bluetooth.taskMutex, 0) != pdTRUE) {
        ESP_LOGW(CBT_TAG, "startTask: invalid state");

        return BT_INVALID_STATE;
    }

    xSemaphoreGive(Bluetooth.taskMutex);
    Bluetooth.btState = BT_RUN;
  
    if(Bluetooth.eventCallbacks.onConnect) {
        Bluetooth.eventCallbacks.onConnect();
    }
  
    xTaskCreate(sendTask, "sendTask", BT_STACK_SIZE, NULL, BT_TASK_PRIORITY, NULL);

    ESP_LOGI(CBT_TAG, "startTask: Complete");

    return BT_OK;
}

uint16_t CBluetooth::stopTask()
{
    //set kill flag
    Bluetooth.connectionType  = BT_NONE;
    Bluetooth.btState         = BT_LISTEN;

    if(Bluetooth.eventCallbacks.onDisconnect) {
        Bluetooth.eventCallbacks.onDisconnect();
    }

    //with kill flag set we need to send a message to activate the task
    CPacket* packet = new CPacket();
    xQueueSend(Bluetooth.sppSendQueue, &packet, portMAX_DELAY);
    xSemaphoreTake(Bluetooth.taskMutex, portMAX_DELAY);
    xSemaphoreGive(Bluetooth.taskMutex);

    ESP_LOGI(CBT_TAG, "stopTask: Complete");

    return BT_OK;
}

void CBluetooth::bleGapEventHandler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    esp_err_t err;
  
    ESP_LOGD(CBT_TAG, "bleGapEventHandler: event %d", event);
    switch (event) {
    case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
        ESP_ERROR_CHECK(esp_ble_gap_start_advertising(&Bluetooth.sppAdvParams));
        break;

    case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        //advertising start complete event to indicate advertising start successfully or failed
        if((err = param->adv_start_cmpl.status) != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGW(CBT_TAG, "bleGapEventHandler: advertising start failed: %s", esp_err_to_name(err));
        }
        break;

    default:
        break;
    }
  }

void CBluetooth::btGapEventHandler(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param)
{
    char bda_str[18] = { 0 };
  
    ESP_LOGD(CBT_TAG, "btGapEventHandler: event [%x]", event);
    switch (event) {
    case ESP_BT_GAP_AUTH_CMPL_EVT: 
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            ESP_LOGI(CBT_TAG, "btGapEventHandler: authentication success: %s bda [%s]", param->auth_cmpl.device_name, bda2str(param->auth_cmpl.bda, bda_str, sizeof(bda_str)));
        }
        else {
            ESP_LOGE(CBT_TAG, "btGapEventHandler: authentication failed, status [%d]", param->auth_cmpl.stat);
        }
        break;
  
    case ESP_BT_GAP_PIN_REQ_EVT: 
        ESP_LOGI(CBT_TAG, "btGapEventHandler: ESP_BT_GAP_PIN_REQ_EVT min_16_digit [%d]", param->pin_req.min_16_digit);
        esp_bt_gap_pin_reply(param->pin_req.bda, true, Bluetooth.btPasswordLength, Bluetooth.btPassword);
        break;
  
    case ESP_BT_GAP_MODE_CHG_EVT:
        ESP_LOGI(CBT_TAG, "btGapEventHandler: ESP_BT_GAP_MODE_CHG_EVT mode [%d] bda [%s]", param->mode_chg.mode, bda2str(param->mode_chg.bda, bda_str, sizeof(bda_str)));
        break;
  
    default:
        break;
    }
}

void CBluetooth::bleGattsProfileEventHandler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    esp_ble_gatts_cb_param_t *p_data = (esp_ble_gatts_cb_param_t *) param;
    uint8_t res = 0xff;

    ESP_LOGD(CBT_TAG, "bleGattsProfileEventHandler: event [%x]",event);
    switch (event) {
    case ESP_GATTS_REG_EVT:
        ESP_LOGD(CBT_TAG, "bleGattsProfileEventHandler: ESP_GATTS_REG_EVT");
        xSemaphoreTake(Bluetooth.settingsMutex, portMAX_DELAY);
        ESP_ERROR_CHECK(esp_ble_gap_set_device_name(Bluetooth.bleGapName));
        ESP_ERROR_CHECK(esp_ble_gap_config_adv_data_raw((uint8_t *)Bluetooth.sppAdvData, Bluetooth.sppAdvData[7] + 8));
        xSemaphoreGive(Bluetooth.settingsMutex);

        ESP_ERROR_CHECK(esp_ble_gatts_create_attr_tab(Bluetooth.sppGattDB, gatts_if, SPP_IDX_NB, SPP_SVC_INST_ID));
        break;
      
    case ESP_GATTS_READ_EVT:
        res = Bluetooth.findCharAndDesrIndex(p_data->read.handle);
        //if(res == SPP_IDX_SPP_STATUS_VAL){
            //TODO:client read the status characteristic
        //}
        break;
      
    case ESP_GATTS_WRITE_EVT: 
        {
            res = Bluetooth.findCharAndDesrIndex(p_data->write.handle);
            if(p_data->write.is_prep == false) {
                ESP_LOGD(CBT_TAG, "bleGattsProfileEventHandler: ESP_GATTS_WRITE_EVT - handle [%d]", res);
                /*if (res == SPP_IDX_SPP_COMMAND_VAL) {
                    //do nothing on spp commands
                    ESP_LOGW(CBT_TAG, "bleGattsProfileEventHandler: ESP_GATTS_WRITE_EVT - SPP COMMAND [%d]", p_data->write.len);
                } else */if(res == SPP_IDX_SPP_DATA_NTF_CFG) {
                    if(((p_data->write.len == 2)&&(p_data->write.value[0] == 0x01)&&(p_data->write.value[1] == 0x00)) ||
                        ((p_data->write.len == 2) && (p_data->write.value[0] == 0x02) && (p_data->write.value[1] == 0x00))) {
                        Bluetooth.bleEnableNotification();
                    }
                    else {
                        Bluetooth.bleDisableNotification();
                    }
                } else if(res == SPP_IDX_SPP_DATA_RECV_VAL) {
                    if (Bluetooth.connectionType == BT_BLE && Bluetooth.eventCallbacks.dataReceived) {
                        CPacket* packet = new CPacket(p_data->write.value, p_data->write.len, false);
                        Bluetooth.eventCallbacks.dataReceived(packet);
                        delete packet;
                    }
                } else {
                //TODO:
                }
            } else if((p_data->write.is_prep == true)&&(res == SPP_IDX_SPP_DATA_RECV_VAL)) {
                ESP_LOGD(CBT_TAG, "bleGattsProfileEventHandler: ESP_GATTS_PREP_WRITE_EVT - handle [%d]", res);
                Bluetooth.bleStoreWRBuffer(p_data);
            }
        }
        break;
    
    case ESP_GATTS_EXEC_WRITE_EVT:
        ESP_LOGD(CBT_TAG, "bleGattsProfileEventHandler: ESP_GATTS_EXEC_WRITE_EVT");
        if(p_data->exec_write.exec_write_flag){
            Bluetooth.bleSendBufferedMessage();
            Bluetooth.bleFreeWriteBuffer();
        }
        break;
      
    case ESP_GATTS_MTU_EVT:
        Bluetooth.bleSppMTUSize = p_data->mtu.mtu;
        break;
      
    case ESP_GATTS_CONF_EVT:
        break;
      
    case ESP_GATTS_UNREG_EVT:
        break;
      
    case ESP_GATTS_DELETE_EVT:
        break;
      
    case ESP_GATTS_START_EVT:
        break;
      
    case ESP_GATTS_STOP_EVT:
        break;
      
    case ESP_GATTS_CONNECT_EVT:
        ESP_LOGI(CBT_TAG, "bleGattsProfileEventHandler: GATTS connecting");
        if (Bluetooth.btAllowConnection && Bluetooth.connectionType == BT_NONE) {
            Bluetooth.connectionType  = BT_BLE;
            Bluetooth.bleSppConnId    = p_data->connect.conn_id;
            Bluetooth.bleSppGattsIf   = gatts_if;
            memcpy(&Bluetooth.sppRemoteBda, &p_data->connect.remote_bda, sizeof(esp_bd_addr_t));
            xSemaphoreGive(Bluetooth.congestedSemaphore);
            ESP_LOGI(CBT_TAG, "bleGattsProfileEventHandler: GATTS connected");
        }
        break;
      
    case ESP_GATTS_DISCONNECT_EVT:
        if (Bluetooth.connectionType == BT_BLE) {
            Bluetooth.bleDisableNotification();
            ESP_LOGI(CBT_TAG, "bleGattsProfileEventHandler: GATTS disconnected");
        }
        break;
      
    case ESP_GATTS_OPEN_EVT:
        break;
      
    case ESP_GATTS_CANCEL_OPEN_EVT:
        break;
      
    case ESP_GATTS_CLOSE_EVT:
        break;
      
    case ESP_GATTS_LISTEN_EVT:
        break;
      
    case ESP_GATTS_CONGEST_EVT:
        if(p_data->connect.conn_id == Bluetooth.bleSppConnId) {
            if(p_data->congest.congested) xSemaphoreTake(Bluetooth.congestedSemaphore, 1);
            else xSemaphoreGive(Bluetooth.congestedSemaphore);
            ESP_LOGI(CBT_TAG, "bleGattsProfileEventHandler: congestion - connection [%d]", p_data->congest.congested);
        } else {
            ESP_LOGW(CBT_TAG, "bleGattsProfileEventHandler: congestion - connection id does not match? [%d]", p_data->connect.conn_id);
        }
        break;
      
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
        ESP_LOGD(CBT_TAG, "bleGattsProfileEventHandler: create attribute table - handle [%x]", param->add_attr_tab.num_handle);
        if (param->add_attr_tab.status != ESP_GATT_OK){
            ESP_LOGE(CBT_TAG, "bleGattsProfileEventHandler: create attribute table failed, error code [%x]", param->add_attr_tab.status);
        }
        else if (param->add_attr_tab.num_handle != SPP_IDX_NB){
            ESP_LOGE(CBT_TAG, "bleGattsProfileEventHandler: create attribute table abnormally, num_handle [%d] doesn't equal to HRS_IDX_NB [%d]", param->add_attr_tab.num_handle, SPP_IDX_NB);
        }
        else {
            memcpy(Bluetooth.sppHandleTable, param->add_attr_tab.handles, sizeof(Bluetooth.sppHandleTable));
            ESP_ERROR_CHECK(esp_ble_gatts_start_service(Bluetooth.sppHandleTable[SPP_IDX_SVC]));
        }
        break;
      
    default:
        break;
    }
}

void CBluetooth::bleGattsEventHandler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param)
{
    ESP_LOGD(CBT_TAG, "bleGattsEventHandler: EVT [%d], gatts if [%d]", event, gatts_if);

    /* If event is register event, store the gatts_if for each profile */
    if (event == ESP_GATTS_REG_EVT) {
        if (param->reg.status == ESP_GATT_OK) {
            Bluetooth.sppProfileTab[SPP_PROFILE_APP_IDX].gatts_if = gatts_if;
        } else {
            ESP_LOGD(CBT_TAG, "bleGattsEventHandler: reg app failed, app_id [%x], status [%d]",param->reg.app_id, param->reg.status);
            return;
        }
    }

    do {
        int idx;
        for (idx = 0; idx < SPP_PROFILE_NUM; idx++) {
            if (gatts_if == ESP_GATT_IF_NONE || /* ESP_GATT_IF_NONE, not specify a certain gatt_if, need to call every profile cb function */
                gatts_if == Bluetooth.sppProfileTab[idx].gatts_if) {
                if (Bluetooth.sppProfileTab[idx].gatts_cb) {
                    Bluetooth.sppProfileTab[idx].gatts_cb(event, gatts_if, param);
                }
            }
        }
    } while (0);
}

void CBluetooth::btSppEventHandler(esp_spp_cb_event_t event, esp_spp_cb_param_t* param)
{
    char bda_str[18] = { 0 };

    switch (event) {
    case ESP_SPP_INIT_EVT:
        if (param->init.status == ESP_SPP_SUCCESS) {
            ESP_LOGD(CBT_TAG, "btSppEventHandler: ESP_SPP_INIT_EVT");
            esp_spp_start_srv(Bluetooth.sppSecMask, Bluetooth.sppRoleSlave, 0, Bluetooth.btGapName);
        }
        else {
            ESP_LOGE(CBT_TAG, "btSppEventHandler: ESP_SPP_INIT_EVT status [%d]", param->init.status);
            ESP_LOGE(CBT_TAG, "btSppEventHandler: esp_restart");
            esp_restart();
        }
        break;
      
    case ESP_SPP_DISCOVERY_COMP_EVT:
        ESP_LOGD(CBT_TAG, "btSppEventHandler: ESP_SPP_DISCOVERY_COMP_EVT");
        break;
      
    case ESP_SPP_OPEN_EVT:
        ESP_LOGD(CBT_TAG, "btSppEventHandler: ESP_SPP_OPEN_EVT");
        break;
      
    case ESP_SPP_CLOSE_EVT:
        ESP_LOGW(CBT_TAG, "btSppEventHandler: ESP_SPP_CLOSE_EVT status [%d] handle [%lu] close_by_remote [%d]", param->close.status, param->close.handle, param->close.async);
        if (Bluetooth.connectionType == BT_CLASSIC && param->close.handle == Bluetooth.btClassicHandle) {
            Bluetooth.btClassicHandle = 0;
            Bluetooth.stopTask();
            Bluetooth.btAllowConnection = true;
        }
        break;
      
    case ESP_SPP_START_EVT:
        if (param->start.status == ESP_SPP_SUCCESS) {
            ESP_LOGD(CBT_TAG, "btSppEventHandler: ESP_SPP_START_EVT handle [%lu] sec_id [%d] scn [%d]", param->start.handle, param->start.sec_id, param->start.scn);
            esp_bt_dev_set_device_name(Bluetooth.btGapName);
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
        }
        else {
            ESP_LOGE(CBT_TAG, "btSppEventHandler: ESP_SPP_START_EVT status [%d]", param->start.status);
        }
        break;
      
    case ESP_SPP_CL_INIT_EVT:
        ESP_LOGD(CBT_TAG, "btSppEventHandler: ESP_SPP_CL_INIT_EVT");
        break;
      
    case ESP_SPP_DATA_IND_EVT:
        ESP_LOGD(CBT_TAG, "btSppEventHandler: ESP_SPP_DATA_IND_EVT received [%d] from [%lu]", param->data_ind.len, param->srv_open.handle);
        if (Bluetooth.connectionType == BT_CLASSIC && Bluetooth.btClassicHandle == param->srv_open.handle) {
            if(Bluetooth.eventCallbacks.dataReceived) {
                CPacket* packet = new CPacket(param->data_ind.data, param->data_ind.len, false);
                Bluetooth.eventCallbacks.dataReceived(packet);
                delete packet;
            }
        } else {
            ESP_LOGE(CBT_TAG, "btSppEventHandler: ESP_SPP_DATA_IND_EVT - invalid connection");
        }
        break;
        
    case ESP_SPP_CONG_EVT:
        ESP_LOGD(CBT_TAG, "btSppEventHandler: ESP_SPP_CONG_EVT");
        break;
        
    case ESP_SPP_WRITE_EVT:
        ESP_LOGD(CBT_TAG, "btSppEventHandler: ESP_SPP_WRITE_EVT");
        break;
        
    case ESP_SPP_SRV_OPEN_EVT:
        ESP_LOGI(CBT_TAG, "btSppEventHandler: ESP_SPP_SRV_OPEN_EVT status [%d] handle[%lu], rem_bda: [%s] ", param->srv_open.status, param->srv_open.handle, Bluetooth.bda2str(param->srv_open.rem_bda, bda_str, sizeof(bda_str)));
        if (Bluetooth.btAllowConnection && Bluetooth.connectionType == BT_NONE) {
            Bluetooth.connectionType      = BT_CLASSIC;
            Bluetooth.btClassicHandle     = param->srv_open.handle;
            Bluetooth.btAllowConnection   = false;
            Bluetooth.startTask();
            xSemaphoreGive(Bluetooth.congestedSemaphore);
        } else {
            ESP_LOGI(CBT_TAG, "btSppEventHandler: ESP_SPP_SRV_OPEN_EVT connection not allowed");
            esp_spp_disconnect(param->srv_open.handle);
        }
        break;
      
    case ESP_SPP_SRV_STOP_EVT:
        ESP_LOGD(CBT_TAG, "btSppEventHandler: ESP_SPP_SRV_STOP_EVT");
        break;
        
    case ESP_SPP_UNINIT_EVT:
        ESP_LOGD(CBT_TAG, "btSppEventHandler: ESP_SPP_UNINIT_EVT");
        break;
        
    default:
        break;
    }
}

char* CBluetooth::bda2str(uint8_t* bda, char* str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }

    uint8_t* p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x", p[0], p[1], p[2], p[3], p[4], p[5]);
    
    return str;
}

uint8_t CBluetooth::findCharAndDesrIndex(uint16_t handle)
{
    for (int i = 0; i < SPP_IDX_NB; i++) {
        if (handle == Bluetooth.sppHandleTable[i]) {
            return i;
        }
    }

    //return error
    return 0xff;
}

void CBluetooth::bleEnableNotification()
{
    if (Bluetooth.connectionType == BT_BLE) {
        Bluetooth.enable_data_ntf = true;

        Bluetooth.startTask();
    }
}

void CBluetooth::bleDisableNotification()
{
    if (Bluetooth.connectionType == BT_BLE) {
        Bluetooth.enable_data_ntf = false;

        Bluetooth.stopTask();

        Bluetooth.bleSppMTUSize = DEFAULT_MTU_SIZE;

        if (Bluetooth.btAllowConnection)
            ESP_ERROR_CHECK(esp_ble_gap_start_advertising(&Bluetooth.sppAdvParams));
    }
}

bool CBluetooth::bleStoreWRBuffer(esp_ble_gatts_cb_param_t* p_data)
{
    spp_receive_data_node_t* temp_spp_recv_data_node_p1 = (spp_receive_data_node_t*)malloc(sizeof(spp_receive_data_node_t));

    if (temp_spp_recv_data_node_p1 == NULL) {
        ESP_LOGE(CBT_TAG, "bleStoreWRBuffer: malloc error");
        return false;
    }

    temp_spp_recv_data_node_p1->len = p_data->write.len;
    Bluetooth.sppRecvDataBuff.buff_size += p_data->write.len;
    temp_spp_recv_data_node_p1->next_node = NULL;
    temp_spp_recv_data_node_p1->node_buff = (uint8_t*)malloc(p_data->write.len);
    Bluetooth.sppRecvLastNode = temp_spp_recv_data_node_p1;
    memcpy(temp_spp_recv_data_node_p1->node_buff, p_data->write.value, p_data->write.len);
    if (Bluetooth.sppRecvDataBuff.node_num == 0) {
        Bluetooth.sppRecvDataBuff.first_node = temp_spp_recv_data_node_p1;
    }
    Bluetooth.sppRecvDataBuff.node_num++;

    return true;
}

void CBluetooth::bleFreeWriteBuffer()
{
    spp_receive_data_node_t* temp_spp_recv_data_node_p1 = Bluetooth.sppRecvDataBuff.first_node;
    spp_receive_data_node_t* temp_spp_recv_data_node_p2 = NULL;
    
    while (temp_spp_recv_data_node_p1 != NULL) {
        temp_spp_recv_data_node_p2 = temp_spp_recv_data_node_p1->next_node;
        free(temp_spp_recv_data_node_p1->node_buff);
        free(temp_spp_recv_data_node_p1);
        temp_spp_recv_data_node_p1 = temp_spp_recv_data_node_p2;
    }

    Bluetooth.sppRecvDataBuff.node_num    = 0;
    Bluetooth.sppRecvDataBuff.buff_size   = 0;
    Bluetooth.sppRecvDataBuff.first_node  = NULL;
    Bluetooth.sppRecvLastNode             = NULL;
}

void CBluetooth::bleSendBufferedMessage()
{
    if (!Bluetooth.sppRecvDataBuff.first_node) {
        ESP_LOGW(CBT_TAG, "bleSendBufferedMessage: no data available");

        return;
    }

    if (Bluetooth.connectionType == BT_BLE && Bluetooth.eventCallbacks.dataReceived) {
        spp_receive_data_node_t* temp_spp_recv_data_node_p1 = Bluetooth.sppRecvDataBuff.first_node;

        CPacket* pkt = new CPacket(Bluetooth.sppRecvDataBuff.buff_size);
        if (!pkt || !pkt->valid()) {
            ESP_LOGE(CBT_TAG, "bleSendBufferedMessage: malloc error");
            return;
        }

        uint32_t recv_len = 0;
        while (temp_spp_recv_data_node_p1) {
            if (recv_len > pkt->size)
                continue;

            memcpy(pkt->data + recv_len, (char*)(temp_spp_recv_data_node_p1->node_buff), temp_spp_recv_data_node_p1->len);
            recv_len += temp_spp_recv_data_node_p1->len;
            temp_spp_recv_data_node_p1 = temp_spp_recv_data_node_p1->next_node;
        }


        Bluetooth.eventCallbacks.dataReceived(pkt);
        delete pkt;

        ESP_LOGD(CBT_TAG, "bleSendBufferedMessage: sending message with length [%lu]", recv_len);
    }
}

uint16_t CBluetooth::clearQueue()
{
    if (!Bluetooth.sppSendQueue) {
        return BT_INVALID_QUEUE;
    }

    CPacket* packet;
    while (xQueueReceive(Bluetooth.sppSendQueue, &packet, 0) == pdTRUE) {
        if (packet) {
            delete packet;
        }
    }

    return BT_OK;
}

#endif