#include "communications.h"
#include "communications_command_ota.h"

#define OTA_TAG "OTATask"

CComsCommandOTA::CComsCommandOTA(uint8_t cmd, uint32_t timeout) : CComsCommand(cmd, timeout)
{
    updateHandle        = 0;
    updatePartition     = NULL;
    runningPartition    = NULL;
    configuredPartition = NULL;
}

CComsCommandOTA::~CComsCommandOTA()
{
    CComsCommand::~CComsCommand();
}

COMReturn CComsCommandOTA::start(CPacket* packet)
{
    packetNumber = 0;

    configuredPartition = esp_ota_get_boot_partition();
    runningPartition = esp_ota_get_running_partition();
    if (configuredPartition != runningPartition) {
        ESP_LOGW(OTA_TAG, "startOTA(): configured OTA boot partition at offset 0x%08lx, but running from offset 0x%08lx", configuredPartition->address, runningPartition->address);
        ESP_LOGW(OTA_TAG, "startOTA(): (this can happen if either the OTA boot data or preferred boot image become corrupted somehow.)");
    }

    ESP_LOGI(OTA_TAG, "Running partition type %d subtype %d (offset 0x%08lx)", runningPartition->type, runningPartition->subtype, runningPartition->address);

    updatePartition = esp_ota_get_next_update_partition(NULL);
    if (!updatePartition) {
        ESP_LOGE(OTA_TAG, "startOTA(): update partition is invalid");

        return COM_ERROR;
    }

    ESP_LOGI(OTA_TAG, "startOTA(): writing to partition subtype %d at offset 0x%lx", updatePartition->subtype, updatePartition->address);

    //check current packet size
    if (packet->size() != sizeof(uint32_t) + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t) + sizeof(esp_app_desc_t)) {
        ESP_LOGE(OTA_TAG, "startOTA(): invalid header size [%u]", packet->size());

        return COM_ERROR;
    }

    //check packet crc32
    if (!CRC32.check(packet->data() + sizeof(uint32_t), packet->size() - sizeof(uint32_t), ((uint32_t*)packet->data())[0])) {
        ESP_LOGE(OTA_TAG, "startOTA(): packet failed CRC32 check");

        return COM_ERROR;
    }

    // check current version with downloading
    esp_app_desc_t new_app_info;
    memcpy(&new_app_info, &packet->data()[sizeof(uint32_t) + sizeof(esp_image_header_t) + sizeof(esp_image_segment_header_t)], sizeof(esp_app_desc_t));
    ESP_LOGI(OTA_TAG, "startOTA(): new firmware version: %s", new_app_info.version);

    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(runningPartition, &running_app_info) == ESP_OK) {
        ESP_LOGI(OTA_TAG, "startOTA(): running firmware version: %s", running_app_info.version);
    }

    const esp_partition_t* last_invalid_app = esp_ota_get_last_invalid_partition();
    esp_app_desc_t invalid_app_info;
    if (esp_ota_get_partition_description(last_invalid_app, &invalid_app_info) == ESP_OK) {
        ESP_LOGI(OTA_TAG, "startOTA(): last invalid firmware version: %s", invalid_app_info.version);
    }

    // check current version with last invalid partition
    if (last_invalid_app != NULL) {
        if (memcmp(invalid_app_info.version, new_app_info.version, sizeof(new_app_info.version)) == 0) {
            ESP_LOGW(OTA_TAG, "startOTA(): new version is the same as invalid version.");
            ESP_LOGW(OTA_TAG, "startOTA(): previously, there was an attempt to launch the firmware with %s version, but it failed.", invalid_app_info.version);
            ESP_LOGW(OTA_TAG, "startOTA(): the firmware has been rolled back to the previous version.");

            return COM_ERROR;
        }
    }

    if (memcmp(new_app_info.version, running_app_info.version, sizeof(new_app_info.version)) == 0) {
        ESP_LOGW(OTA_TAG, "startOTA(): current running version is the same as a new. we will not continue the update.");

        return COM_ERROR;
    }

    esp_err_t err = esp_ota_begin(updatePartition, OTA_WITH_SEQUENTIAL_WRITES, &updateHandle);
    if (err != ESP_OK) {
        ESP_LOGE(OTA_TAG, "startOTA(): esp_ota_begin failed [%s]", esp_err_to_name(err));
        esp_ota_abort(updateHandle);

        return COM_ERROR;
    }

    ESP_LOGI(OTA_TAG, "startOTA(): esp_ota_begin succeeded");

    err = esp_ota_write(updateHandle, packet->data() + sizeof(uint32_t), packet->size() - sizeof(uint32_t));
    if (err != ESP_OK) {
        ESP_LOGE(OTA_TAG, "startOTA(): esp_ota_write failed [%s]", esp_err_to_name(err));
        esp_ota_abort(updateHandle);

        return COM_ERROR;
    }

    packet->clear();

    ESP_LOGI(OTA_TAG, "startOTA(): successful");

    return COM_OK;
}

COMReturn CComsCommandOTA::end(CPacket* packet)
{
    packet->clear();

    ESP_LOGI(OTA_TAG, "endOTA(): complete");

    return COM_OK;
}

COMReturn CComsCommandOTA::idle(CPacket* packet)
{
    return CComsCommand::idle(packet);
}

COMReturn CComsCommandOTA::receive(CPacket* packet)
{
    switch (packet->data()[0]) {
    case 0x01:
        receivePacket(packet);

        return COM_OK;
        break;
    }

    return COM_ERROR;
}


COMReturn CComsCommandOTA::receivePacket(CPacket* packet)
{
    //check current packet size
    if (packet->size() < sizeof(uint32_t) + sizeof(uint16_t)) {
        ESP_LOGE(OTA_TAG, "startOTA(): invalid header size [%u]", packet->size());

        return COM_ERROR;
    }

    //confirm packet number

    //check packet crc32
    if (!CRC32.check(packet->data() + sizeof(uint32_t), packet->size() - sizeof(uint32_t), ((uint32_t*)packet->data())[0])) {
        ESP_LOGE(OTA_TAG, "startOTA(): packet failed CRC32 check");

        return COM_ERROR;
    }

    esp_err_t err = esp_ota_write(updateHandle, packet->data() + sizeof(uint32_t), packet->size() - sizeof(uint32_t));
    if (err != ESP_OK) {
        ESP_LOGE(OTA_TAG, "startOTA(): esp_ota_write failed [%s]", esp_err_to_name(err));
        esp_ota_abort(updateHandle);

        return COM_ERROR;
    }

    packet->clear();

    return COM_OK;
}