#ifndef COMMUNICATIONS_GLOBALS_H
#define COMMUNICATIONS_GLOBALS_H

#include "packet.h"
#include "globals.h"

#include "bluetooth.h"
#include "wifiap.h"
#include "crc32.h"

//BLE send queue size
#define COMS_HEADER_ID					0xBEEF
#define COM_QUEUE_SIZE                  8
#define COMS_STACK_SIZE                 3072
#define COM_TASK_PRIORITY               0
#define COM_TASK_TICK_TIME				10
#define COM_PACKET_TIMEOUT				1000
#define COMS_DEFAULT_PKT_SIZE			(1024 * 8)
#define COMS_TAG						"Communications"

//command types
typedef enum {
	COM_COMMAND_START_TASK				= 0x00,
	COM_COMMAND_CURRENT_TASK			= 0xFD,
	COM_COMMAND_INVALID_PACKET			= 0xFE,
	COM_COMMAND_END_TASK				= 0xFF
} COMCommand;

//response types
typedef enum {
	COM_RESPONSE_OK						= 0x01,
	COM_RESPONSE_WAIT					= 0x02,
	COM_RESPONSE_COMPLETE				= 0x03,
	COM_RESPONSE_TASK_ALREADY_RUNNING	= 0xFA,
	COM_RESPONSE_CURRENT_TASK			= 0xFB,
	COM_RESPONSE_PACKET_TIMEOUT			= 0xFC,
	COM_RESPONSE_TASK_NOT_STARTED		= 0xFB,
	COM_RESPONSE_INVALID_COMMAND		= 0xFE,
	COM_RESPONSE_INVALID_PACKET			= 0xFF,
} COMResponse;

typedef enum {
	COM_DEINIT,
	COM_INIT,
	COM_RUN
} COMState;

typedef enum {
	COM_OK,
	COM_WAIT,
	COM_COMPLETE,
	COM_TIMEOUT,
	COM_ERROR,
	COM_ERROR_INVALID,
	COM_ERROR_NOT_FOUND,
	COM_ERROR_MALLOC
} COMReturn;

typedef struct {
	uint16_t	id;
	uint16_t	size;
	uint32_t	crc;
} COMHeader;

typedef struct {
	COMHeader	header;
	uint8_t		command;
} COMHeaderCommand;

typedef COMReturn	(*CComsCallback)(CPacket* packet, uint32_t tick);

#endif