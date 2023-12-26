#ifndef FAT32_H
#define FAT32_H

#include "sdmmc_cmd.h"
#include "globals.h"

#define BUS_FREQUENCY			40000
#define MAX_OPEN_FILES			5
#define MOUNT_POINT				"/sdcard"

#define FAT_RET_OK              0
#define FAT_RET_FAILED          1
#define FAT_RET_INVALID_ARG     2
#define FAT_RET_INVALID_STATE   3
#define FAT_RET_NOT_MOUNTED     4

#include "globals.h"

class CDirEntry {
public:
	uint32_t	fileSize;
	uint16_t	nameLength;
	bool		isDirectory;
};

class CFat32 {
public:
	CFat32();
	~CFat32();

	static int		mount();
	static int		unmount();
	static int		listDir(const char* directory, uint8_t levels = 1);
	static int		listDir(const char* directory, uint8_t levels, uint8_t** buffer, uint16_t* length);
	static int		createDir(const char* directory);
	static int		removeDir(const char* directory);
	static int		renameFile(const char* path1, const char* path2);
	static int		deleteFile(const char* path);

private:
	static int		listDir(const char* directory, uint8_t levels, uint8_t offset, uint8_t** buffer, uint16_t length, uint16_t* position);
	static int		addEntry(uint8_t** buffer, uint16_t* length, uint16_t* position, uint32_t size, char* name, bool directory);
	static char*	makeName(const char* directory, const char* file);

	sdmmc_card_t*	pSDCard;
};

extern CFat32 Fat32;

#endif
