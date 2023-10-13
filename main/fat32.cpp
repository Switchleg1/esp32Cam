#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_vfs_fat.h"
#include "esp_vfs_dev.h"

#include "driver/sdmmc_host.h"
#include "fat32.h"

#define CFAT_TAG  "CFat32"

CFat32 Fat32;

CFat32::CFat32()
{
    if (this != &Fat32) {
        ESP_LOGW(CFAT_TAG, "CFat32: Only one instance of CFat32 allowed");
        return;
    }

    Fat32.pSDCard = NULL;
}

CFat32::~CFat32()
{
    if (this != &Fat32) {
        ESP_LOGW(CFAT_TAG, "~CFat32: Only one instance of CFat32 allowed");
        return;
    }

    unmount();
}

int CFat32::mount()
{
    if (Fat32.pSDCard) {
        ESP_LOGW(CFAT_TAG, "mount: already mounted");
        return FAT_RET_OK;
    }

    ESP_LOGI(CFAT_TAG, "mount: attemption to mount [%s]", MOUNT_POINT);

    sdmmc_host_t host   = SDMMC_HOST_DEFAULT();
    host.flags          = SDMMC_HOST_FLAG_4BIT;
    host.slot           = SDMMC_HOST_SLOT_1;
    host.max_freq_khz   = SDMMC_FREQ_HIGHSPEED;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width   = 4;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed     = true,
        .max_files                  = MAX_OPEN_FILES,
        .allocation_unit_size       = 0
    };

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(MOUNT_POINT, &host, &slot_config, &mount_config, &Fat32.pSDCard);
    switch (ret) {
        case ESP_OK:
            // Card has been initialized, print its properties
            ESP_LOGI(CFAT_TAG, "fat32_mount: CID name %s!\n", Fat32.pSDCard->cid.name);
            ESP_LOGI(CFAT_TAG, "fat32_mount: mounted sd card at [%s]", MOUNT_POINT);
            return FAT_RET_OK;

        case ESP_ERR_INVALID_STATE:
            ESP_LOGE(CFAT_TAG, "fat32_mount: File system already mounted");
            return FAT_RET_INVALID_STATE;

        case ESP_FAIL:
            ESP_LOGE(CFAT_TAG, "Failed to mount filesystem. If you want the card to be formatted, set format_if_mount_failed = true.");
            return FAT_RET_FAILED;
    }

    ESP_LOGE(CFAT_TAG, "Failed to initialize the card (%d). Make sure SD card lines have pull-up resistors in place.", ret);

    return FAT_RET_FAILED;
}

int CFat32::unmount()
{
    if (Fat32.pSDCard) {
        esp_err_t ret = esp_vfs_fat_sdcard_unmount(MOUNT_POINT, Fat32.pSDCard);
        Fat32.pSDCard = NULL;

        if (ret != ESP_ERR_INVALID_STATE) {
            return FAT_RET_OK;
        }
    }

    ESP_LOGE(CFAT_TAG, "File system not mounted");

    return FAT_RET_NOT_MOUNTED;
}

int CFat32::listDir(const char* directory, uint8_t levels)
{
    return Fat32.listDir(directory, levels, 0, NULL, 0, NULL);
}

int CFat32::listDir(const char* directory, uint8_t levels, uint8_t** buffer, uint16_t* length)
{
    uint16_t buffLen = *length;
    *length = 0;
    return Fat32.listDir(directory, levels, 0, buffer, buffLen, length);
}

int CFat32::listDir(const char* directory, uint8_t levels, uint8_t offset, uint8_t** buffer, uint16_t length, uint16_t *position)
{
    DIR* dp = opendir(directory);
    if (dp != NULL) {
        if (!buffer) ESP_LOGI(CFAT_TAG, "listDir:%*s[%s]", offset, " ", directory);

        struct dirent* ep;
        while ((ep = readdir(dp)) != NULL) {
            switch (ep->d_type) {
            case DT_DIR:
                {
                    if (!buffer) ESP_LOGI(CFAT_TAG, "listDir:%*s DIR: %s", offset, " ", ep->d_name);

                    char* pName = makeName(directory, ep->d_name);
                    if (pName) {
                        addEntry(buffer, &length, position, 0, pName, true);
                        if (levels > 1) Fat32.listDir(pName, levels - 1, offset + 4, buffer, length, position);
                        free(pName);
                    }
                }
                break;

            default:
                {
                    uint32_t sz = 0;
                    char* pName = makeName(directory, ep->d_name);
                    if (pName) {
                        FILE* f = fopen(pName, "r");
                        if (f) {
                            fseek(f, 0L, SEEK_END);
                            sz = ftell(f);
                            fclose(f);
                        }

                        addEntry(buffer, &length, position, sz, pName, false);
                        free(pName);
                    }

                    if (!buffer) ESP_LOGI(CFAT_TAG, "listDir:%*s   FILE: %s [%lu]", offset, " ", ep->d_name, sz);
                }
            }
        }
        closedir(dp);
    }
    else {
        if (!buffer) ESP_LOGE(CFAT_TAG, "listDir:%*sFailed to open directory [%s]!", offset, " ", directory);

        return FAT_RET_FAILED;
    }

    if (!buffer) ESP_LOGI(CFAT_TAG, "listDir:%*s---------------", offset, " ");

    return FAT_RET_OK;
}

int CFat32::addEntry(uint8_t** buffer, uint16_t* length, uint16_t* position, uint32_t size, char* name, bool directory)
{
    if (buffer && *buffer) {
        uint16_t bufferLengthRequired = *position + strlen(name) + sizeof(CDirEntry);
        if (*length < bufferLengthRequired) {
            uint16_t newLength = bufferLengthRequired + 1024;
            uint8_t* newBuffer = (uint8_t*)malloc(newLength);
            if (!newBuffer) {
                return false;
            }
            memcpy(newBuffer, *buffer, *length);
            free(*buffer);
            *buffer = newBuffer;
            *length = newLength;
        }

        CDirEntry* header   = (CDirEntry*)(*buffer + *position);
        header->fileSize    = size;
        header->nameLength  = strlen(name);
        header->isDirectory = directory;
        *position           += sizeof(CDirEntry);
        memcpy(*buffer + *position, name, header->nameLength);
        *position           += header->nameLength;

        return FAT_RET_OK;
    }

    return FAT_RET_FAILED;
}

char* CFat32::makeName(const char* directory, const char* file)
{
    char* pName = (char*)malloc(strlen(directory) + strlen(file) + 3);
    if (pName) {
        if (directory[strlen(directory) - 1] != '/') {
            sprintf(pName, "%s/%s/", directory, file);
        }
        else {
            sprintf(pName, "%s%s/", directory, file);
        }
        return pName;
    }

    return NULL;
}

int CFat32::createDir(const char* directory)
{
  ESP_LOGI(CFAT_TAG, "createDir: Creating [%s]", directory);
  if(!mkdir(directory, S_IRWXU)){
    ESP_LOGI(CFAT_TAG, "createDir: Dir created");

    return FAT_RET_OK;
  } else {
    ESP_LOGI(CFAT_TAG, "createDir: mkdir failed");

    return FAT_RET_FAILED;
  }
}

int CFat32::removeDir(const char* directory)
{
  ESP_LOGI(CFAT_TAG, "removeDir: Removing [%s]", directory);
  if(!rmdir(directory)){
    ESP_LOGI(CFAT_TAG, "removeDir: Dir removed");

    return FAT_RET_OK;
  } else {
    ESP_LOGI(CFAT_TAG, "removeDir: rmdir failed");

    return FAT_RET_FAILED;
  }
}

int CFat32::renameFile(const char* path1, const char* path2)
{
    ESP_LOGI(CFAT_TAG, "renameFile: Renaming file %s to %s", path1, path2);
    if (rename(path1, path2)) {
        ESP_LOGI(CFAT_TAG, "renameFile: file renamed");

        return FAT_RET_OK;
    } else {
        ESP_LOGI(CFAT_TAG, "renameFile: rename failed");

        return FAT_RET_FAILED;
    }
}

int CFat32::deleteFile(const char * path)
{
    ESP_LOGI(CFAT_TAG, "deleteFile: Deleting file: %s", path);
    if(!remove(path)){
        ESP_LOGI(CFAT_TAG, "deleteFile: file deleted");

        return FAT_RET_OK;
    } else {
        ESP_LOGI(CFAT_TAG, "deleteFile: delete failed");

        return FAT_RET_FAILED;
    }
}