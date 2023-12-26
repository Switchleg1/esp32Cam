#include "wifiap.h"

#define AP_TAG              "Wifi_AP"
#define AP_RECV_BUFFER_SIZE 4128

CWifiAP WifiAP;

//-------------- Tasks ----------------------//

CWifiAP::CWifiAP()
{
    apState             = AP_DEINIT;
    wifiChannel         = AP_WIFI_CHANNEL_DEFAULT;
    apAllowConnection   = true;
    sendQueue           = NULL;
    recvTaskMutex       = NULL;
    sendTaskMutex       = NULL;
    settingsMutex       = NULL;
    wifiNetif           = NULL;
    bufferRecv          = NULL;
    socketClient        = -1;
    memset(&eventCallbacks, 0, sizeof(CWifiCallbacks));

    if (this != &WifiAP) {
        ESP_LOGW(AP_TAG, "CWifiAP: Only one instance of CWifiAP can be used");
        return;
    }
}

CWifiAP::~CWifiAP()
{
    stopModem();
    deinitWifi();
}

uint16_t CWifiAP::initWifi()
{
    if (WifiAP.apState != AP_DEINIT) {
        ESP_LOGI(AP_TAG, "initWifi: invalid state");

        return AP_INVALID_STATE;
    }

    //create mutexes and semaphores
    WifiAP.recvTaskMutex    = xSemaphoreCreateMutex();
    WifiAP.sendTaskMutex    = xSemaphoreCreateMutex();
    WifiAP.settingsMutex    = xSemaphoreCreateMutex();
    WifiAP.sendQueue        = xQueueCreate(AP_QUEUE_SIZE, sizeof(CPacket*));
    WifiAP.bufferRecv       = (uint8_t*)malloc(AP_RECV_BUFFER_SIZE);

    if (WifiAP.recvTaskMutex && WifiAP.sendTaskMutex && WifiAP.settingsMutex && WifiAP.sendQueue && WifiAP.bufferRecv) {
        WifiAP.apState = AP_INIT;

        ESP_LOGD(AP_TAG, "initWifi: success");

        return AP_OK;
    }
    
    ESP_LOGD(AP_TAG, "initWifi: error");
   
    return AP_ERROR;
}

uint16_t CWifiAP::deinitWifi()
{
    if (WifiAP.apState != AP_INIT) {
        ESP_LOGI(AP_TAG, "deinitWifi: invalid state");

        return AP_INVALID_STATE;
    }

    bool didDeInit = false;

    if (WifiAP.sendQueue) {
        vQueueDelete(WifiAP.sendQueue);
        WifiAP.sendQueue    = NULL;
        didDeInit           = true;
    }

    if (WifiAP.recvTaskMutex) {
        vSemaphoreDelete(WifiAP.recvTaskMutex);
        WifiAP.recvTaskMutex    = NULL;
        didDeInit               = true;
    }

    if (WifiAP.sendTaskMutex) {
        vSemaphoreDelete(WifiAP.sendTaskMutex);
        WifiAP.sendTaskMutex    = NULL;
        didDeInit               = true;
    }

    if (WifiAP.settingsMutex) {
        vSemaphoreDelete(WifiAP.settingsMutex);
        WifiAP.settingsMutex    = NULL;
        didDeInit               = true;
    }

    if (WifiAP.bufferRecv) {
        free(WifiAP.bufferRecv);
        WifiAP.bufferRecv   = NULL;
        didDeInit           = true;
    }

    WifiAP.apState = AP_DEINIT;

    if (didDeInit) {
        ESP_LOGD(AP_TAG, "deinitWifi: success");

        return AP_OK;
    }

    ESP_LOGD(AP_TAG, "deinitWifi: error");

    return AP_ERROR;
}

uint16_t CWifiAP::startModem(CWifiCallbacks callbacks, const char* SSID, const char* password)
{
    if (WifiAP.apState != AP_INIT) {
        ESP_LOGI(AP_TAG, "startModem: invalid state");

        return AP_INVALID_STATE;
    }

    memcpy(&WifiAP.eventCallbacks, &callbacks, sizeof(CWifiCallbacks));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    WifiAP.wifiNetif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &WifiAP.eventHandler,
        NULL,
        NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .channel = (uint8_t)WifiAP.wifiChannel,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .max_connection = AP_MAX_STA_CONN,
        },
    };

    //set SSID
    memset(wifi_config.ap.ssid, 0, 32);
    if (SSID) {
        wifi_config.ap.ssid_len = strlen(SSID);
        if (wifi_config.ap.ssid_len > 32) {
            wifi_config.ap.ssid_len = 32;
        }
        memcpy(wifi_config.ap.ssid, SSID, wifi_config.ap.ssid_len);
    }
    else {
        wifi_config.ap.ssid_len = strlen(AP_WIFI_SSID_KEY);
        memcpy(wifi_config.ap.ssid, AP_WIFI_SSID_KEY, wifi_config.ap.ssid_len);
    }
    
    //set password
    memset(wifi_config.ap.password, 0, 64);
    if (password) {
        uint8_t passLength = strlen(password);
        if (passLength > 64) {
            passLength = 64;
        }

        memcpy(wifi_config.ap.password, password, passLength);

        if (passLength < 8) {
            memset(wifi_config.ap.password + passLength, '0', 8 - passLength);
            passLength = 8;
        }
    }

    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_inactive_time(WIFI_IF_AP, AP_CONNECTION_TIMEOUT));

    //set default connection status
    WifiAP.apAllowConnection    = true;
    WifiAP.apState              = AP_LISTEN;

    ESP_LOGI(AP_TAG, "startModem: success");

    return AP_OK;
}

uint16_t CWifiAP::stopModem()
{
    if (WifiAP.apState < AP_LISTEN) {
        ESP_LOGI(AP_TAG, "stopModem: invalid state");

        return AP_INVALID_STATE;
    }

    bool bStopModem = false;

    WifiAP.stopTask();

    if (esp_wifi_stop() == ESP_OK) {
        bStopModem = true;
    }
    
    if (esp_wifi_deinit() == ESP_OK) {
        bStopModem = true;
    }

    if (WifiAP.wifiNetif) {
        esp_netif_destroy_default_wifi(WifiAP.wifiNetif);
        WifiAP.wifiNetif    = NULL;
        bStopModem          = true;
    }

    if (esp_event_loop_delete_default() == ESP_OK) {
        bStopModem = true;
    }
    
    if (esp_netif_deinit() == ESP_OK) {
        bStopModem = true;
    }

    clearQueue();

    WifiAP.apState = AP_INIT;

    if (bStopModem) {
        ESP_LOGI(AP_TAG, "stopModem: success");

        return AP_OK;
    }

    ESP_LOGW(AP_TAG, "stopModem: error");
    
    return AP_ERROR;
}

uint16_t CWifiAP::sendData(CPacket* packet)
{
    if (!WifiAP.isConnected()) {
        ESP_LOGW(AP_TAG, "sendData: not connected");
        return AP_INVALID_STATE;
    }

    if (!WifiAP.sendQueue || xQueueSend(WifiAP.sendQueue, &packet, pdMS_TO_TICKS(AP_SEND_TIMEOUT)) != pdTRUE) {
        ESP_LOGE(AP_TAG, "sendData: queue is full");
        return AP_QUEUE_FULL;
    }

    return AP_OK;
}

uint16_t CWifiAP::currentState()
{
    return WifiAP.apState;
}

uint16_t CWifiAP::isConnected()
{
    return WifiAP.apState == AP_RUN;
}

void CWifiAP::allowConnection(bool allow)
{
    WifiAP.apAllowConnection = allow;
}

void CWifiAP::setChannel(uint16_t channel)
{
    if (channel == AP_WIFI_CHANNEL_DEFAULT) {
        channel = esp_random();
    }

    WifiAP.wifiChannel = channel % 14;

    if (WifiAP.wifiChannel < 1) {
        WifiAP.wifiChannel = 1;
    }
}

uint16_t CWifiAP::getChannel()
{
    return WifiAP.wifiChannel;
}

//------------ Private functions ------------//

void CWifiAP::recvTask(void* pvParameters)
{
    //subscribe to WDT
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    ESP_ERROR_CHECK(esp_task_wdt_status(NULL));

    xSemaphoreTake(WifiAP.recvTaskMutex, portMAX_DELAY);
    WifiAP.clearQueue();

    int32_t hSocket = -1;
    if (WifiAP.createSocket(&hSocket, AP_PORT) != AP_OK) {
        ESP_LOGE(AP_TAG, "recvTask: unable to create socket.");
        goto finish_task;
    }

    while (WifiAP.apState == AP_RUN) {
        //check if we are allowed to accept new connections
        if (!WifiAP.apAllowConnection) {
            esp_task_wdt_reset();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        fd_set fds;
        int n;
        struct timeval tv;

        // Set up the file descriptor set.
        FD_ZERO(&fds);
        FD_SET(hSocket, &fds);

        // Set up the struct timeval for the timeout.
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        // Wait until timeout or data received.
        n = select(hSocket + 1, &fds, NULL, NULL, &tv);
        if (n > 0) {
            struct sockaddr_storage source_addr; // Large enough for both IPv4 or IPv6
            socklen_t addr_len = sizeof(source_addr);
            WifiAP.socketClient = accept(hSocket, (struct sockaddr*)&source_addr, &addr_len);
            if (WifiAP.socketClient < 0) {
                ESP_LOGW(AP_TAG, "recvTask: unable to accept connection [%d]", errno);
            }
            else {
                ESP_LOGI(AP_TAG, "recvTask: connected");
                WifiAP.clearQueue();
            }

            while (WifiAP.socketClient > 0) {
                FD_ZERO(&fds);
                FD_SET(WifiAP.socketClient, &fds);

                tv.tv_sec = 1;
                tv.tv_usec = 0;

                n = select(WifiAP.socketClient + 1, &fds, NULL, NULL, &tv);
                if (n > 0) {
                    int16_t sizeRead = recv(WifiAP.socketClient, WifiAP.bufferRecv, AP_RECV_BUFFER_SIZE, 0);
                    if (sizeRead < 0) {
                        ESP_LOGE(AP_TAG, "recvTask: recv error [%d] %s", sizeRead, strerror(errno));
                        close(WifiAP.socketClient);
                        WifiAP.socketClient = -1;
                    }
                    else {
                        ESP_LOGD(AP_TAG, "recvTask: recv [%d]", sizeRead);
                        if (WifiAP.eventCallbacks.dataReceived) {
                            CPacket* packet = new CPacket(WifiAP.bufferRecv, sizeRead, false);
                            WifiAP.eventCallbacks.dataReceived(packet);
                            delete packet;
                        }
                    }
                }
                else if (n < 0) {
                    ESP_LOGE(AP_TAG, "recvTask: socket closed");
                    close(WifiAP.socketClient);
                    WifiAP.socketClient = -1;
                }
                else {
                    ESP_LOGD(AP_TAG, "recvTask: no data");
                }

                esp_task_wdt_reset();
                taskYIELD();
            }
        }
        else {
            ESP_LOGD(AP_TAG, "recvTask: waiting for conection");
        }

        //reset the WDT and yield to tasks
        esp_task_wdt_reset();
        taskYIELD();
    }

finish_task:
    WifiAP.clearQueue();

    if (WifiAP.socketClient >= 0) {
        close(WifiAP.socketClient);
        WifiAP.socketClient = -1;
    }

    if (hSocket >= 0) {
        close(hSocket);
    }

    xSemaphoreGive(WifiAP.recvTaskMutex);

    //unsubscribe to WDT and delete task
    ESP_ERROR_CHECK(esp_task_wdt_delete(NULL));
    vTaskDelete(NULL);
}

void CWifiAP::sendTask(void* pvParameters)
{
    //subscribe to WDT
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    ESP_ERROR_CHECK(esp_task_wdt_status(NULL));

    xSemaphoreTake(WifiAP.sendTaskMutex, portMAX_DELAY);
    WifiAP.clearQueue();

    CPacket* packet;
    while (WifiAP.apState == AP_RUN) {
        if (xQueueReceive(WifiAP.sendQueue, &packet, pdMS_TO_TICKS(AP_RECEIVE_TIMEOUT)) == pdTRUE) {
            if (packet) {
                if (WifiAP.apState == AP_RUN) {
                    ESP_LOGD(AP_TAG, "sendTask: send [%u]", packet->size());
                    if (send(WifiAP.socketClient, packet->data(), packet->size(), 0) != packet->size()) {
                        ESP_LOGE(AP_TAG, "sendTask: did not send all data");
                    }
                }

                delete packet;
            }
        }

        //reset the WDT and yield to tasks
        esp_task_wdt_reset();
        taskYIELD();
    }

    WifiAP.clearQueue();
    xSemaphoreGive(WifiAP.sendTaskMutex);

    //unsubscribe to WDT and delete task
    ESP_ERROR_CHECK(esp_task_wdt_delete(NULL));
    vTaskDelete(NULL);
}

void CWifiAP::eventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    switch (event_id) {
    case WIFI_EVENT_AP_STACONNECTED:
    {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*)event_data;
        ESP_LOGI(AP_TAG, "eventHandler: station " MACSTR " join, AID=%u", MAC2STR(event->mac), event->aid);

        WifiAP.startTask();
    }
    break;

    case WIFI_EVENT_AP_STADISCONNECTED:
    {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*)event_data;
        ESP_LOGI(AP_TAG, "eventHandler: station " MACSTR " leave, AID=%u", MAC2STR(event->mac), event->aid);

        WifiAP.stopTask();
    }
    break;
    }
}

uint16_t CWifiAP::startTask()
{
    if (WifiAP.apState != AP_LISTEN) {
        ESP_LOGW(AP_TAG, "startTask: invalid state");
        return AP_INVALID_STATE;
    }

    if (xSemaphoreTake(WifiAP.recvTaskMutex, 0) != pdTRUE) {
        ESP_LOGW(AP_TAG, "startTask: recv task running");
        return AP_INVALID_STATE;
    }
    xSemaphoreGive(WifiAP.recvTaskMutex);

    if (xSemaphoreTake(WifiAP.sendTaskMutex, 0) != pdTRUE) {
        ESP_LOGW(AP_TAG, "startTask: send task running");
        return AP_INVALID_STATE;
    }
    xSemaphoreGive(WifiAP.sendTaskMutex);

    WifiAP.apState = AP_RUN;
    if (WifiAP.eventCallbacks.onConnect) {
        WifiAP.eventCallbacks.onConnect();
    }
    xTaskCreate(WifiAP.sendTask, "sendTask", AP_STACK_SIZE, NULL, AP_TASK_PRIORITY, NULL);
    xTaskCreate(WifiAP.recvTask, "recvTask", AP_STACK_SIZE, NULL, AP_TASK_PRIORITY, NULL);

    ESP_LOGI(AP_TAG, "startTask: complete");

    return AP_OK;
}

uint16_t CWifiAP::stopTask()
{
    if (WifiAP.apState != AP_RUN) {
        ESP_LOGW(AP_TAG, "stopTask: invalid state");
        return AP_INVALID_STATE;
    }

    //set kill flag
    WifiAP.apState = AP_LISTEN;
    if (WifiAP.eventCallbacks.onDisconnect) {
        WifiAP.eventCallbacks.onDisconnect();
    }

    //with kill flag set we need to send a message to activate the task
    CPacket* packet = new CPacket();
    xQueueSend(WifiAP.sendQueue, &packet, portMAX_DELAY);
    xSemaphoreTake(WifiAP.recvTaskMutex, portMAX_DELAY);
    xSemaphoreTake(WifiAP.sendTaskMutex, portMAX_DELAY);
    xSemaphoreGive(WifiAP.sendTaskMutex);
    xSemaphoreGive(WifiAP.recvTaskMutex);

    ESP_LOGI(AP_TAG, "stopTask: complete");

    return AP_OK;
}

uint16_t CWifiAP::createSocket(int32_t* outSocket, uint32_t port)
{
    int ip_protocol = 0;
    struct sockaddr_storage dest_addr;

    struct sockaddr_in* dest_addr_ip4 = (struct sockaddr_in*)&dest_addr;
    dest_addr_ip4->sin_addr.s_addr = htonl(INADDR_ANY);
    dest_addr_ip4->sin_family = AF_INET;
    dest_addr_ip4->sin_port = htons(port);
    ip_protocol = IPPROTO_IP;
    *outSocket = socket(AF_INET, SOCK_STREAM, ip_protocol);
    if (*outSocket < 0) {
        ESP_LOGE(AP_TAG, "createSocket: unable to create socket: errno %d", errno);
        return AP_ERROR;
    }
    int opt = 1;
    setsockopt(*outSocket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    fcntl(*outSocket, F_SETFL, O_NONBLOCK);

    ESP_LOGI(AP_TAG, "createSocket: socket created");

    int err = bind(*outSocket, (struct sockaddr*)&dest_addr, sizeof(dest_addr));
    if (err != 0) {
        ESP_LOGE(AP_TAG, "createSocket: socket unable to bind: errno %d", errno);
        close(*outSocket);
        *outSocket = -1;
        return AP_ERROR;
    }
    ESP_LOGI(AP_TAG, "createSocket: socket bound, port %lu", port);

    err = listen(*outSocket, 1);
    if (err != 0) {
        ESP_LOGE(AP_TAG, "createSocket: error occurred during listen: errno %d", errno);
        close(*outSocket);
        *outSocket = -1;
        return AP_ERROR;
    }

    return AP_OK;
}

uint16_t CWifiAP::clearQueue()
{
    if (!WifiAP.sendQueue) {
        return AP_ERROR;
    }

    CPacket* packet;
    while (xQueueReceive(WifiAP.sendQueue, &packet, 0) == pdTRUE) {
        if (packet) {
            delete packet;
        }
    }

    return AP_OK;
}