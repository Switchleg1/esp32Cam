#ifndef COMMUNICATIONS_COMMAND_H
#define COMMUNICATIONS_COMMAND_H

#include "communications_globals.h"

class CComsCommand {
public:
	CComsCommand(uint8_t cmd, uint32_t timeout);
	virtual ~CComsCommand();

	uint8_t				command();
	virtual COMReturn	start(CPacket* packet);
	virtual COMReturn	end(CPacket* packet);
	virtual COMReturn	idle(CPacket* packet);
	virtual COMReturn	receive(CPacket* packet);

protected:
	bool				started();

private:
	uint8_t				commandHex;
	uint32_t			maxTimeout;
	uint32_t			currentTimeout;
	bool				isStarted;
};

#endif