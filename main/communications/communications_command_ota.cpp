#include "communications.h"
#include "communications_command_ota.h"

#define OTA_TAG "OTATask"

CComsCommandOTA::CComsCommandOTA(uint8_t cmd, uint32_t timeout) : CComsCommand(cmd, timeout)
{
}

CComsCommandOTA::~CComsCommandOTA()
{
    CComsCommand::~CComsCommand();
}

COMReturn CComsCommandOTA::start(CPacket* packet)
{
    //clear task variables
    otaWriteLength  = 0;
    packetNumber    = 0;
    updateHandle    = 0;
    updatePartition = NULL;

    //check current packet size
    if (packet->size() != sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
        ESP_LOGE(OTA_TAG, "startOTA(): invalid header size [%u]", packet->size());
        packet->clear();

        return COM_ERROR;
    }

    const esp_partition_t* configuredPartition = esp_ota_get_boot_partition();
    const esp_partition_t* runningPartition    = esp_ota_get_running_partition();
    if (configuredPartition != runningPartition) {
        ESP_LOGW(OTA_TAG, "startOTA(): configured OTA boot partition at offset 0x%08lx, but running from offset 0x%08lx", configuredPartition->address, runningPartition->address);
        ESP_LOGW(OTA_TAG, "startOTA(): (this can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }

    ESP_LOGI(OTA_TAG, "Running partition type %d subtype %d (offset 0x%08lx)", runningPartition->type, runningPartition->subtype, runningPartition->address);

    updatePartition = esp_ota_get_next_update_partition(NULL);
    if (!updatePartition) {
        ESP_LOGE(OTA_TAG, "startOTA(): update partition is invalid");
        packet->clear();

        return COM_ERROR;
    }

    ESP_LOGI(OTA_TAG, "startOTA(): writing to partition subtype %d at offset 0x%lx", updatePartition->subtype, updatePartition->address);

    // check current version with downloading
    esp_app_desc_t newAppInfo;
    memcpy(&newAppInfo, &packet->data()[sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
    ESP_LOGI(OTA_TAG, "startOTA(): new firmware version [%s]", newAppInfo.version);

    esp_app_desc_t runningAppInfo;
    if (esp_ota_get_partition_description(runningPartition, &runningAppInfo) == ESP_OK) {
        ESP_LOGI(OTA_TAG, "startOTA(): running firmware version [%s]", runningAppInfo.version);
    }

    const esp_partition_t* last_invalid_app = esp_ota_get_last_invalid_partition();
    esp_app_desc_t invalidAppInfo;
    if (esp_ota_get_partition_description(last_invalid_app, &invalidAppInfo) == ESP_OK) {
        ESP_LOGI(OTA_TAG, "startOTA(): last invalid firmware version [%s]", invalidAppInfo.version);
    }

    /*// check new version with last invalid partition
    if (CHECK_OTA_VERSION && last_invalid_app != NULL) {
        if (memcmp(invalidAppInfo.version, newAppInfo.version, sizeof(newAppInfo.version)) == 0) {
            ESP_LOGW(OTA_TAG, "startOTA(): new version is the same as invalid version.");
            ESP_LOGW(OTA_TAG, "startOTA(): previously, there was an attempt to launch the firmware with [%s] version, but it failed.", invalidAppInfo.version);
            ESP_LOGW(OTA_TAG, "startOTA(): the firmware has been rolled back to the previous version.");

            packet->clear();

            return COM_ERROR;
        }
    }

    // check new version with current partition
    if (CHECK_OTA_VERSION && memcmp(newAppInfo.version, runningAppInfo.version, sizeof(newAppInfo.version)) == 0) {
        ESP_LOGW(OTA_TAG, "startOTA(): current running version is the same as a new. we will not continue the update.");

        packet->clear();

        return COM_ERROR;
    }*/

    // request to begin the process
    esp_err_t err = esp_ota_begin(updatePartition, OTA_WITH_SEQUENTIAL_WRITES, &updateHandle);
    if (err != ESP_OK) {
        ESP_LOGE(OTA_TAG, "startOTA(): esp_ota_begin failed [%s]", esp_err_to_name(err));
        esp_ota_abort(updateHandle);
        packet->clear();

        return COM_ERROR;
    }
    ESP_LOGI(OTA_TAG, "startOTA(): esp_ota_begin succeeded");

    // write the header we received
    err = esp_ota_write(updateHandle, packet->data(), packet->size());
    if (err != ESP_OK) {
        ESP_LOGE(OTA_TAG, "startOTA(): esp_ota_write failed [%s]", esp_err_to_name(err));
        esp_ota_abort(updateHandle);
        packet->clear();

        return COM_ERROR;
    }
    otaWriteLength += packet->size();

    packet->clear();

    ESP_LOGI(OTA_TAG, "startOTA(): successful");

    return CComsCommand::start(packet);
}

COMReturn CComsCommandOTA::end(CPacket* packet)
{
    packet->clear();

    ESP_LOGI(OTA_TAG, "endOTA(): complete");

    return CComsCommand::end(packet);
}

COMReturn CComsCommandOTA::idle(CPacket* packet)
{
    return CComsCommand::idle(packet);
}

COMReturn CComsCommandOTA::receive(CPacket* packet)
{
    uint8_t command = packet->data()[0];
    packet->forward(1);

    switch (command) {
    case 0x10:
        return receivePacket(packet);
        break;

    case 0x11:
        return endOTA(packet);
        break;
    }

    ESP_LOGE(OTA_TAG, "receive(): invalid packet [%u]", packet->size());
    packet->clear();

    return COM_OK;
}


COMReturn CComsCommandOTA::receivePacket(CPacket* packet)
{
    //check current packet size
    if (packet->size() < sizeof(uint16_t)) {
        ESP_LOGE(OTA_TAG, "receivePacket(): invalid header size [%u]", packet->size());
        packet->clear();

        return COM_ERROR;
    }

    //confirm packet number
    uint16_t currentPacketNumber = *((uint16_t*)packet->data());
    if (packetNumber != currentPacketNumber) {
        ESP_LOGE(OTA_TAG, "receivePacket(): incorrect packet number [%u:%u]", currentPacketNumber, packetNumber);
        
        uint8_t newData[3];
        newData[0] = 0xFF;
        *((uint16_t*)(newData + 1)) = packetNumber;
        packet->copy(newData, 3);

        return CComsCommand::receive(packet);
    }

    //write data to OTA partition
    packet->forward(sizeof(uint16_t));
    esp_err_t err = esp_ota_write(updateHandle, packet->data(), packet->size());
    if (err != ESP_OK) {
        ESP_LOGE(OTA_TAG, "receivePacket(): esp_ota_write failed [%s]", esp_err_to_name(err));
        esp_ota_abort(updateHandle);

        packet->clear();

        return COM_ERROR;
    }
    otaWriteLength += packet->size();
    packetNumber++;

    packet->clear();

    ESP_LOGI(OTA_TAG, "receivePacket(): packet [%u], write Position [%lu]", packetNumber, otaWriteLength);

    return CComsCommand::receive(packet);
}

COMReturn CComsCommandOTA::endOTA(CPacket* packet)
{
    if (packet->size() != sizeof(uint32_t)) {
        ESP_LOGI(OTA_TAG, "endOTA(): invalid packet");
        esp_ota_abort(updateHandle);
        packet->clear();

        return COM_ERROR;
    }

    ESP_LOGI(OTA_TAG, "endOTA(): total write binary data length [%lu]", otaWriteLength);
    uint32_t finalWriteLength = *((uint32_t*)packet->data());
    if (otaWriteLength != finalWriteLength) {
        ESP_LOGE(OTA_TAG, "endOTA(): packet count does not match [%lu:%lu]", otaWriteLength, finalWriteLength);
        esp_ota_abort(updateHandle);
        packet->clear();

        return COM_ERROR;
    }

    esp_err_t err = esp_ota_end(updateHandle);
    if (err != ESP_OK) {
        if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
            ESP_LOGE(OTA_TAG, "endOTA(): Image validation failed, image is corrupted");
        }
        else {
            ESP_LOGE(OTA_TAG, "endOTA(): esp_ota_end failed [%s]!", esp_err_to_name(err));
        }
        packet->clear();
       
        return COM_ERROR;
    }

    err = esp_ota_set_boot_partition(updatePartition);
    if (err != ESP_OK) {
        ESP_LOGE(OTA_TAG, "endOTA(): esp_ota_set_boot_partition failed [%s]!", esp_err_to_name(err));
        packet->clear();

        return COM_ERROR;
    }
    ESP_LOGI(OTA_TAG, "endOTA(): Prepare to restart system!");

    esp_restart();

    return COM_COMPLETE;
}