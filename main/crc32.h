#ifndef CRC32_H
#define CRC32_H

#define CRC_DEFAULT_POLYNOMIAL	0x3764F388
#define CRC_DEFAULT_PRE			0x0
#define CRC_DEFAULT_POST		0x0

class CCRC32 {
public:
	static uint32_t build(uint8_t* data, uint32_t size, uint32_t polynomial = CRC_DEFAULT_POLYNOMIAL, uint32_t pre = CRC_DEFAULT_PRE, uint32_t post = CRC_DEFAULT_POST);
	static bool		check(uint8_t* data, uint32_t size, uint32_t crc_in);
};

extern CCRC32 CRC32;

#endif