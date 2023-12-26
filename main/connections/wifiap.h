#ifndef WIFIAP_H
#define WIFIAP_H

#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "packet.h"
#include "globals.h"

#define AP_QUEUE_SIZE					64
#define AP_STACK_SIZE					3072
#define AP_TASK_PRIORITY				2

#define AP_WIFI_SSID_KEY                "MY_WIFI_AP"
#define AP_WIFI_PASS_KEY                "1234"
#define AP_MAX_STA_CONN                 1
#define AP_CONNECTION_TIMEOUT           10
#define AP_RECEIVE_TIMEOUT              1000
#define AP_SEND_TIMEOUT                 100
#define AP_PORT                         1879
#define AP_WIFI_CHANNEL_DEFAULT         1

#define AP_OK                           0
#define AP_ERROR                        1
#define AP_ALLOC_ERROR                  2
#define AP_INVALID_STATE                4
#define AP_QUEUE_FULL                   5

typedef enum {
    AP_DEINIT,
    AP_INIT,
    AP_LISTEN,
    AP_RUN
} WifiConnectionType;

class CWifiCallbacks {
public:
    void (*dataReceived)(CPacket* packet);
    void (*onConnect)();
    void (*onDisconnect)();
};

class CWifiAP {
public:
    CWifiAP();
    ~CWifiAP();

    static uint16_t     initWifi();
    static uint16_t     deinitWifi();
    static uint16_t     startModem(CWifiCallbacks callbacks, const char* SSID, const char* password);
    static uint16_t     stopModem();
    static uint16_t     sendData(CPacket* packet);
    static uint16_t     currentState();
    static uint16_t     isConnected();
    static void         allowConnection(bool allow);
    static void         setChannel(uint16_t channel);
    static uint16_t     getChannel();

private:
    static void         recvTask(void* pvParameters);
    static void         sendTask(void* pvParameters);
    static void         eventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static uint16_t     startTask();
    static uint16_t     stopTask();
    static uint16_t     createSocket(int32_t* outSocket, uint32_t port);
    static uint16_t     clearQueue();
    static uint16_t     buildSSID(uint8_t* ssid, uint8_t* len);
    static uint16_t     buildPassword(uint8_t* ssid, uint8_t* password);

    uint16_t            wifiChannel;
    WifiConnectionType  apState;
    bool                apAllowConnection;
    QueueHandle_t       sendQueue;
    SemaphoreHandle_t   recvTaskMutex;
    SemaphoreHandle_t   sendTaskMutex;
    SemaphoreHandle_t   settingsMutex;
    esp_netif_t*        wifiNetif;
    uint8_t*            bufferRecv;
    int32_t             socketClient;
    CWifiCallbacks      eventCallbacks;
};

extern CWifiAP WifiAP;

#endif