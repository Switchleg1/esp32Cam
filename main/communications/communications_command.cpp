#include "communications_command.h"

CComsCommand::CComsCommand(uint8_t cmd, uint32_t timeout)
{
	commandHex		= cmd;
	maxTimeout		= timeout / COM_TASK_TICK_TIME;
	currentTimeout	= maxTimeout;
	isStarted		= false;
}

CComsCommand::~CComsCommand()
{
}

uint8_t CComsCommand::command()
{
	return commandHex;
}

COMReturn CComsCommand::start(CPacket* packet)
{
	currentTimeout	= maxTimeout;
	isStarted		= true;

	return COM_OK;
}

COMReturn CComsCommand::end(CPacket* packet)
{
	isStarted = false;

	return COM_OK;
}

COMReturn CComsCommand::idle(CPacket* packet)
{
	if (currentTimeout) {
		currentTimeout--;
	} else {
		return COM_TIMEOUT;
	}

	return COM_WAIT;
}

COMReturn CComsCommand::receive(CPacket* packet)
{
	currentTimeout = maxTimeout;

	return COM_OK;
}

bool CComsCommand::started()
{
	return isStarted;
}
