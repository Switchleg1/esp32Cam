#ifndef COMMUNICATIONS_H
#define COMMUNICATIONS_H

#include "communications_globals.h"
#include "communications_command.h"

class CCommunications {
public:
	CCommunications();
	~CCommunications();

	static COMReturn	initComs();
	static COMReturn	deinitComs();
	static COMReturn	startComs();
	static COMReturn	stopComs();

	static COMState		currentState();

	static COMReturn	addCommand(CComsCommand* command);
	static COMReturn	delCommand(uint16_t index);
	static void			clearCommands();
	static uint16_t		countCommand();
	static int32_t		currentCommand();
	static bool			isConnected();

private:
	static void			parseTask(void* vPtr);
	static COMReturn	endCurrentCommand();
	static void			dataReceived(CPacket* packet);
	static COMReturn	sendPacket(CPacket* packet);
	static COMReturn	sendPacket(CPacket* packet, uint8_t cmd);
	static COMReturn	sendResponse(CPacket* packet, uint8_t cmd, uint8_t response);
	static COMReturn	checkPacketHeader(CPacket* packet);
	static COMReturn	callIdle();
	static COMReturn	callCommand(CPacket* packet);

	CComsCommand**		commands;
	uint16_t			commandCount;
	int32_t				commandCurrent;
	uint8_t				commandLast;

	QueueHandle_t		receiveQueue;
	SemaphoreHandle_t	taskMutex;
	COMState			comState;
	CPacket*			partialPacket;
	uint16_t			partialPacketTimeout;
};

extern CCommunications Communications;

#endif