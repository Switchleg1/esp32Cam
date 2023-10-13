#include <string.h>
#include "esp_heap_caps.h"
#include "globals.h"
#include "fat32.h"
#include "bluetooth.h"
#include "wifiap.h"
#include "camera.h"
#include "packet.h"
#include "crc32.h"

#define MAIN_TAG "Main"

//packet header ID
#define PKT_HEADER_ID       0xBEEF
#define PKT_DEFAULT_SIZE    (1024 * 8)
#define TASK_TICK_TIME      5
#define TASK_DELAY_TIME(x)  (x /  TASK_TICK_TIME)

// Header we expect to receive on BLE packets
typedef struct packetHeader {
    uint16_t    id;
    uint16_t    size;
    uint32_t    crc;
} packetHeader_t;

typedef struct fileHeader {
    uint8_t     command;
    uint16_t    packetNumber;
} fileHeader_t;

typedef enum {
    DT_NONE,
    DT_SEND_DIR,
    DT_SEND_FILE,
    DT_SEND_FRAME,
    DT_DELETE_FILE
} DataState;


uint16_t        taskTickCount       = 0;
DataState       dataState           = DT_NONE;
char            fileName[512]       = { 0 };

bool            filePacketSendNext  = false;
bool            filePacketResend    = false;
uint16_t        filePacketNumber    = 0;
FILE*           readFile            = NULL;

bool            framePacketSendNext = false;
uint16_t        framePacketNumber   = 0;
camera_fb_t*    currentFrame        = NULL;

CPacket*        partialPacket       = NULL;

bool isConnected()
{
    if (Bluetooth.isConnected() || WifiAP.isConnected()) {
        return true;
    }

    return false;
}

void clearFrame()
{
    if (currentFrame) {
        esp_camera_fb_return(currentFrame);
        currentFrame = NULL;
    }
}

bool onFrame(camera_fb_t* frame)
{
    if (isConnected()) {
        if (dataState != DT_SEND_FRAME || currentFrame == NULL) {
            ESP_LOGD(MAIN_TAG, "onFrame: grabbed frame");

            clearFrame();
            currentFrame = frame;

            return true;
        }
    }
    else {
        clearFrame();
    }

    return false;
}

int sendPacket(CPacket* packet)
{
    uint8_t timeout = 0;
    int     btRes   = BT_INVALID_STATE;

    do {
        if (Bluetooth.isConnected()) {
            btRes = Bluetooth.send(packet);
        }
        else if (WifiAP.isConnected()) {
            btRes = WifiAP.sendData(packet);
        }

        switch (btRes) {
        case BT_OK:
            ESP_LOGD(MAIN_TAG, "sendPacket: Packet sent");
            timeout = 0;

            break;

        case BT_QUEUE_FULL:
            if (++timeout == 255) {
                ESP_LOGD(MAIN_TAG, "sendPacket: BT timeout");
                delete packet;

                return BT_QUEUE_FULL;
            }
            break;

        case BT_INVALID_STATE:
            ESP_LOGD(MAIN_TAG, "sendPacket: BT not connected");
            delete packet;

            return BT_INVALID_STATE;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        esp_task_wdt_reset();
    } while (btRes != BT_OK);

    return BT_OK;
}

int addHeader(CPacket* packet, uint8_t* command, uint16_t size)
{
    uint8_t* headerData;
    packetHeader_t* header;

    headerData = (uint8_t*)malloc(sizeof(packetHeader_t) + size);
    header          = (packetHeader_t*)headerData;
    header->id      = PKT_HEADER_ID;
    header->size    = packet->size + size;
    memcpy(headerData + sizeof(packetHeader_t), command, size);
    packet->add(headerData, sizeof(packetHeader_t) + size, 0);
    free(headerData);

    header      = (packetHeader_t*)packet->data;
    header->crc = CRC32.build(packet->data + sizeof(packetHeader_t), packet->size - sizeof(packetHeader_t));
    

    return packet->size;
}

void sendDirectory()
{
    CPacket* packet = new CPacket(1024);
    Fat32.listDir(fileName, 1, &packet->data, &packet->size);
    uint8_t command = 0x01;
    uint16_t packetSize = addHeader(packet, &command, 1);

    if (sendPacket(packet) != BT_OK) {
        ESP_LOGI(MAIN_TAG, "sendDirectory: Packet not sent [%u]", packetSize);
        delete packet;
    }
    else {
        ESP_LOGI(MAIN_TAG, "sendDirectory: Packet sent [%u]", packetSize);
    }

    dataState = DT_NONE;
}

void closeFile()
{
    ESP_LOGI(MAIN_TAG, "closeFile: closing file [%s]", fileName);

    filePacketNumber    = 0;
    filePacketResend    = false;

    if (readFile) {
        fclose(readFile);
        readFile = NULL;
    }

    dataState = DT_NONE;
}

void sendFile()
{
    CPacket*        packet;
    fileHeader_t    header;
    uint16_t        packetSize = 0;
    uint8_t         command = 0x02;

    if (!filePacketNumber && !readFile) {
        readFile = fopen(fileName, "r");
        if (!readFile) {
            ESP_LOGE(MAIN_TAG, "sendFile: Unable to open");

            closeFile();

            return;
        }

        ESP_LOGI(MAIN_TAG, "sendFile: Opened file [%s]", fileName);
    }
    else if (!readFile) {
        closeFile();

        return;
    }

    if (filePacketResend) {
        ESP_LOGW(MAIN_TAG, "sendFile: Resending packet [%u]", filePacketNumber);
        filePacketResend = false;
        fseek(readFile, PKT_DEFAULT_SIZE * filePacketNumber, SEEK_SET);
    }

    ESP_LOGD(MAIN_TAG, "sendFile: Reading packet [%u]", filePacketNumber);

    packet          = new CPacket(PKT_DEFAULT_SIZE);
    packet->size    = fread(packet->data, 1, PKT_DEFAULT_SIZE, readFile);
    
    if (packet->size) {
        packetSize          = packet->size;
        header.command      = 0x02;
        header.packetNumber = filePacketNumber;
        addHeader(packet, (uint8_t*)&header, sizeof(fileHeader_t));

        if (sendPacket(packet) == BT_OK) {
            uint32_t dataPosition = (PKT_DEFAULT_SIZE * filePacketNumber) + packetSize;
            ESP_LOGD(MAIN_TAG, "sendFile: Packet sent [%u] [%lu]", packetSize, dataPosition);

            filePacketNumber++;

            return;
        }
        else {
            ESP_LOGW(MAIN_TAG, "sendFile: Unable to send packet");
            
            closeFile();

            return;
        }
        
    }
    else {
        command         = 0x03;
        packetSize      = addHeader(packet, &command, 1);
        packet->size    = sizeof(packetHeader_t) + 1;
        packetSize      = packet->size;

        if (sendPacket(packet) == BT_OK) {
            ESP_LOGI(MAIN_TAG, "sendFile: Send complete [%u]", packetSize);
        }
        else {
            ESP_LOGI(MAIN_TAG, "sendFile: Send complete - failed to send ack [%u]", packetSize);
        }

        closeFile();

        return;
    }
}

void closeFrame()
{
    ESP_LOGI(MAIN_TAG, "closeFrame: closing frame");

    framePacketNumber   = 0;
    dataState           = DT_NONE;
}

void sendFrame()
{
    ESP_LOGD(MAIN_TAG, "sendFrame: Reading packet [%u]", framePacketNumber);
   
    uint32_t packetPosition = framePacketNumber * PKT_DEFAULT_SIZE;
    int32_t dataLeft = currentFrame->len - packetPosition;
    if (dataLeft < 1) dataLeft = 0;
    uint32_t packetSize = PKT_DEFAULT_SIZE < dataLeft ? PKT_DEFAULT_SIZE : dataLeft;

    CPacket* packet = new CPacket(currentFrame->buf + packetPosition, packetSize);
    if (packetSize) {
        fileHeader_t        header;
        header.command      = 0x05;
        header.packetNumber = framePacketNumber;
        addHeader(packet, (uint8_t*)&header, sizeof(fileHeader_t));

        if (sendPacket(packet) == BT_OK) {
            uint32_t dataSentSize = packetPosition + packetSize;
            ESP_LOGI(MAIN_TAG, "sendFrame: Packet sent [%lu] [%lu]", packetSize, dataSentSize);

            framePacketNumber++;

            return;
        }
        else {
            ESP_LOGW(MAIN_TAG, "sendFrame: Unable to send packet");

            closeFrame();

            return;
        }

    }
    else {
        uint8_t command = 0x06;
        packetSize      = addHeader(packet, &command, 1);
        packet->size    = sizeof(packetHeader_t) + 1;
        packetSize      = packet->size;

        if (sendPacket(packet) == BT_OK) {
            ESP_LOGI(MAIN_TAG, "sendFrame: Send complete [%lu]", packetSize);
        }
        else {
            ESP_LOGI(MAIN_TAG, "sendFrame: Send complete - failed to send ack [%lu]", packetSize);
        }

        closeFrame();

        return;
    }
}

void deleteFile()
{
    if (Fat32.deleteFile(fileName) == FAT_RET_OK) {
        ESP_LOGI(MAIN_TAG, "deleteFile: File deleted [%s]", fileName);
    }
    else {
        ESP_LOGI(MAIN_TAG, "deleteFile: File failed to delete [%s]", fileName);
    }

    CPacket* packet = new CPacket();
    uint8_t command = 0x04;
    addHeader(packet, &command, 1);
    packet->size = sizeof(packetHeader_t) + 1;
    uint16_t packetsize = packet->size;
    if (sendPacket(packet) == BT_OK) {
        ESP_LOGI(MAIN_TAG, "deleteFile: Send complete [%u]", packetsize);
    }
    else {
        ESP_LOGI(MAIN_TAG, "deleteFile: Send complete - failed to send ack [%u]", packetsize);
    }

    dataState = DT_NONE;
}

void parsePacket(CPacket* packet)
{
    packetHeader_t* header = (packetHeader_t*)packet->data;
    if (header->id != PKT_HEADER_ID) {
        ESP_LOGW(MAIN_TAG, "dataReceived: invalid header [%x] [%u]", header->id, header->size);
        return;
    }

    uint8_t commandType = packet->data[sizeof(packetHeader_t)];
    switch (dataState) {
    case DT_NONE:
        ESP_LOGD(MAIN_TAG, "dataReceived: [%u] DT_NONE", packet->size);
        switch (commandType) {
        case 0x01:
            ESP_LOGD(MAIN_TAG, "starting task: DT_SEND_DIR");
            dataState = DT_SEND_DIR;
            memcpy(fileName, (char*)&packet->data[sizeof(packetHeader_t) + 1], packet->size - sizeof(packetHeader_t) - 1);
            fileName[packet->size - sizeof(packetHeader_t) - 1] = 0;
            break;

        case 0x02:
            ESP_LOGD(MAIN_TAG, "starting task: DT_SEND_FILE");
            taskTickCount       = 0;
            filePacketNumber    = 0;
            filePacketResend    = false;
            filePacketSendNext  = true;
            if (readFile) {
                fclose(readFile);
                readFile = NULL;
            }
            dataState = DT_SEND_FILE;
            memcpy(fileName, (char*)&packet->data[sizeof(packetHeader_t) + 1], packet->size - sizeof(packetHeader_t) - 1);
            fileName[packet->size - sizeof(packetHeader_t) - 1] = 0;
            break;

        case 0x03:
            ESP_LOGD(MAIN_TAG, "starting task: DT_DELETE_FILE");
            dataState = DT_DELETE_FILE;
            memcpy(fileName, (char*)&packet->data[sizeof(packetHeader_t) + 1], packet->size - sizeof(packetHeader_t) - 1);
            fileName[packet->size - sizeof(packetHeader_t) - 1] = 0;
            break;

        case 0x04:
            ESP_LOGI(MAIN_TAG, "starting task: DT_SEND_FRAME");
            taskTickCount       = 0;
            framePacketNumber   = 0;
            framePacketSendNext = true;
            dataState           = DT_SEND_FRAME;
            break;
        }
        break;

    case DT_SEND_DIR:
        ESP_LOGD(MAIN_TAG, "dataReceived: [%u] DT_SEND_DIR", packet->size);
        break;

    case DT_SEND_FILE:
        ESP_LOGD(MAIN_TAG, "dataReceived: [%u] DT_SEND_FILE", packet->size);
        switch (commandType) {
        case 0x10:
            ESP_LOGD(MAIN_TAG, "dataReceived: DT_SEND_FILE - send next packet");
            filePacketSendNext = true;
            break;

        case 0x11:
            ESP_LOGD(MAIN_TAG, "dataReceived: DT_SEND_FILE - resend packet");
            if (packet->size == sizeof(packetHeader_t) + 3) {
                filePacketNumber    = (packet->data[sizeof(packetHeader_t) + 1] << 8) + packet->data[sizeof(packetHeader_t) + 2];
                filePacketResend    = true;
                filePacketSendNext  = true;
            }
            break;

        case 0x12:
            ESP_LOGD(MAIN_TAG, "dataReceived: DT_SEND_FILE - close frame");
            closeFile();
            break;
        }
        break;

    case DT_SEND_FRAME:
        ESP_LOGD(MAIN_TAG, "dataReceived: [%u] DT_SEND_FRAME", packet->size);
        switch (commandType) {
        case 0x15:
            ESP_LOGD(MAIN_TAG, "dataReceived: DT_SEND_FRAME - send next packet");
            framePacketSendNext = true;
            break;

        case 0x16:
            ESP_LOGD(MAIN_TAG, "dataReceived: DT_SEND_FRAME - resend packet");
            if (packet->size == sizeof(packetHeader_t) + 3) {
                framePacketNumber = (packet->data[sizeof(packetHeader_t) + 1] << 8) + packet->data[sizeof(packetHeader_t) + 2];
                framePacketSendNext = true;
            }
            break;

        case 0x17:
            ESP_LOGD(MAIN_TAG, "dataReceived: DT_SEND_FRAME - close frame");
            closeFrame();
            break;
        }
        break;

    case DT_DELETE_FILE:
        ESP_LOGI(MAIN_TAG, "dataReceived: [%u] DT_DELETE_FILE", packet->size);
        break;
    }
}

void dataReceived(CPacket* packet)
{
    if (!packet) {
        ESP_LOGW(MAIN_TAG, "dataReceived: packet null");
        return;
    }

    uint16_t packetPosition = 0;
    while (packetPosition < packet->size) {
        if (partialPacket) {
            packetHeader_t* partialHeader = (packetHeader_t*)partialPacket->data;
            uint16_t partialLeft = partialHeader->size + sizeof(packetHeader_t) - partialPacket->size - packetPosition;
            uint16_t copyLength = packet->size > partialLeft ? partialLeft : packet->size;
            partialPacket->add(packet->data, copyLength, packetPosition);

            if (copyLength == partialLeft) {
                if (!CRC32.check(partialPacket->data + sizeof(packetHeader_t), partialHeader->size, partialHeader->crc)) {
                    ESP_LOGW(MAIN_TAG, "dataReceived: invalid crc");
                }
                else {
                    parsePacket(partialPacket);
                }
                delete partialPacket ;
                partialPacket = NULL;
            }
            packetPosition += copyLength;
        }
        else {
            CPacket* p = new CPacket(packet->data + packetPosition, packet->size - packetPosition, false);

            if (!p->valid() || p->size < sizeof(packetHeader_t)) {
                ESP_LOGW(MAIN_TAG, "dataReceived: invalid packet");
                delete p;
                return;
            }

            packetHeader_t* header = (packetHeader_t*)p->data;

            if (p->size < header->size + sizeof(packetHeader_t)) {
                ESP_LOGI(MAIN_TAG, "dataReceived: partial start");
                partialPacket   = new CPacket(p->data, p->size, true);
                packetPosition  += p->size;
            }
            else {
                if (!CRC32.check(p->data + sizeof(packetHeader_t), header->size, header->crc)) {
                    ESP_LOGW(MAIN_TAG, "dataReceived: invalid crc");
                }
                else {
                    parsePacket(p);
                }
                packetPosition += header->size + sizeof(packetHeader_t);
            }

            delete p;
        }
    }
}

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

#ifdef CONFIG_BT_ENABLED
    CBluetoothCB btCallbacks = {
        .dataReceived = dataReceived,
        .onConnect    = NULL,
        .onDisconnect = NULL
    };
    Bluetooth.initBT();
    Bluetooth.setBTPin("0123", 4);
    Bluetooth.setSendDelay(0);
    Bluetooth.startModem(btCallbacks, BT_MODE_BLE);
#else
    CWifiCallbacks wifiCallbacks = {
        .dataReceived   = dataReceived,
        .onConnect      = NULL,
        .onDisconnect   = NULL
    };
    WifiAP.initWifi();
    WifiAP.startModem(wifiCallbacks, "MiWifiIsFly", "0123456789");
#endif

    Camera.setOnFrameCallback(onFrame);
    
    while (1) {
        if (!Camera.isRunning()) {
            Camera.start();
        }

        Camera.setAllowMotion(!isConnected());
    
        if (!(taskTickCount % 1000)) {
            ESP_LOGI(MAIN_TAG, "Free heap: %d", heap_caps_get_free_size(MALLOC_CAP_8BIT));
            ESP_LOGI(MAIN_TAG, "Free RAM: %d", heap_caps_get_free_size(MALLOC_CAP_8BIT) - heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            ESP_LOGI(MAIN_TAG, "Free PSRAM: %d", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }

        switch (dataState) {
        case DT_NONE:
            break;

        case DT_SEND_DIR:
            sendDirectory();
            break;

        case DT_SEND_FILE:
            if (filePacketSendNext) {
                filePacketSendNext  = false;
                taskTickCount       = 0;
                sendFile();
            } else if (taskTickCount > TASK_DELAY_TIME(3000)) {
                closeFile();
            }
            break;

        case DT_SEND_FRAME:
            if (framePacketSendNext && currentFrame) {
                framePacketSendNext = false;
                taskTickCount       = 0;
                sendFrame();
            }
            else if (taskTickCount > TASK_DELAY_TIME(3000)) {
                closeFrame();
            }
            break;

        case DT_DELETE_FILE:
            deleteFile();
            break;
        };

        taskTickCount++;
        
        vTaskDelay(pdMS_TO_TICKS(TASK_TICK_TIME));
        esp_task_wdt_reset();
    }

    //unsubscribe to WDT and deinit
    ESP_ERROR_CHECK(esp_task_wdt_delete(NULL));
}
