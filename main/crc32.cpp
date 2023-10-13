#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "crc32.h"

#define CRC_TAG	"CRC32"

CCRC32 CRC32;

uint32_t CCRC32::build(uint8_t* data, uint32_t size, uint32_t polynomial, uint32_t pre, uint32_t post)
{
	uint32_t crc = pre;
	uint32_t i, j;
	for (i = 0;i < size;i++) {
		uint8_t ch = data[i];
		for (j = 0;j < 8;j++) {
			uint32_t b = (ch ^ crc) & 1;
			crc >>= 1;
			if (b) crc = crc ^ polynomial;
			ch >>= 1;
		}
	}

	crc = crc ^ post;
	ESP_LOGD(CRC_TAG, "build: crc [%lx]", crc);

	return crc;
}

bool CCRC32::check(uint8_t* data, uint32_t size, uint32_t crc_in)
{
	uint32_t crc = CRC32.build(data, size, CRC_DEFAULT_POLYNOMIAL, CRC_DEFAULT_PRE, CRC_DEFAULT_POST);
	if (crc == crc_in) {
		ESP_LOGD(CRC_TAG, "check: crc pass [%lx] [%lx]", crc_in, crc);
		return true;
	}
	else {
		ESP_LOGW(CRC_TAG, "check: crc failed [%lx] [%lx]", crc_in, crc);
		return false;
	}
}