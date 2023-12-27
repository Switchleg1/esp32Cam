#include "communications.h"

CCommunications Communications;

CCommunications::CCommunications()
{
	commands				= NULL;
	commandCount			= 0;
	commandCurrent			= -1;

	receiveQueue			= NULL;
	taskMutex				= NULL;
	comState				= COM_DEINIT;
	partialPacket			= NULL;
	partialPacketTimeout	= 0;

	if (this != &Communications) {
		ESP_LOGE(COMS_TAG, "CCommunications(): Cannot create more than one instance of CCommunications()");
		return;
	}
}

CCommunications::~CCommunications()
{
	stopComs();
	deinitComs();
	clearCommands();
}

COMReturn CCommunications::initComs()
{
	if (Communications.comState != COM_DEINIT) {
		ESP_LOGD(COMS_TAG, "initComs(): invalid state");

		return COM_ERROR;
	}

#ifdef CONFIG_BT_ENABLED
	if (Bluetooth.currentState() == BT_DEINIT) {
		if (Bluetooth.initBT() != BT_OK) {
			ESP_LOGE(COMS_TAG, "initComs(): unable to init Bluetooth");

			return COM_ERROR;
		}
		Bluetooth.setBTPin("0123", 4);
		Bluetooth.setSendDelay(0);
	}
#else
	if (WifiAP.currentState() == AP_DEINIT) {
		if (WifiAP.initWifi() != AP_OK) {
		
			ESP_LOGE(COMS_TAG, "initComs(): unable to init Wifi");

			return COM_ERROR;
		}
	}
#endif

	Communications.receiveQueue	= xQueueCreate(COM_QUEUE_SIZE, sizeof(CPacket*));
	Communications.taskMutex	= xSemaphoreCreateMutex();
	Communications.comState		= COM_INIT;

	return COM_OK;
}

COMReturn CCommunications::deinitComs()
{
	if (Communications.comState != COM_INIT) {
		ESP_LOGD(COMS_TAG, "deinitComs(): invalid state");

		return COM_ERROR_INVALID;
	}

#ifdef CONFIG_BT_ENABLED
	Bluetooth.deinitBT();
#else
	WifiAP.deinitWifi();
#endif

	if (Communications.taskMutex) {
		vSemaphoreDelete(Communications.taskMutex);
		Communications.taskMutex = NULL;
	}

	if (Communications.receiveQueue) {
		vQueueDelete(Communications.receiveQueue);
		Communications.receiveQueue = NULL;
	}

	if (Communications.partialPacket) {
		delete Communications.partialPacket;
		Communications.partialPacket = NULL;
	}

	Communications.partialPacketTimeout = 0;
	Communications.comState				= COM_DEINIT;

	return COM_OK;
}

COMReturn CCommunications::startComs()
{
	if (Communications.comState != COM_INIT) {
		ESP_LOGD(COMS_TAG, "startComs(): invalid state");

		return COM_ERROR_INVALID;
	}

#ifdef CONFIG_BT_ENABLED
	CBluetoothCB btCallbacks = {
		.dataReceived	= Communications.dataReceived,
		.onConnect		= NULL,
		.onDisconnect	= NULL
	};
	Bluetooth.startModem(btCallbacks, BT_MODE_BLE);
#else
	CWifiCallbacks wifiCallbacks = {
		.dataReceived	= Communications.dataReceived,
		.onConnect		= NULL,
		.onDisconnect	= NULL
	};
	WifiAP.startModem(wifiCallbacks, "MiWifiIsFly", "0123456789");
#endif

	Communications.comState = COM_RUN;

	xTaskCreate(parseTask, "ComsParseTask", COMS_STACK_SIZE, NULL, COM_TASK_PRIORITY, NULL);

	return COM_OK;
}

COMReturn CCommunications::stopComs()
{
	if (Communications.comState != COM_RUN) {
		ESP_LOGD(COMS_TAG, "stopComs(): invalid state");

		return COM_ERROR_INVALID;
	}

#ifdef CONFIG_BT_ENABLED
	Bluetooth.stopModem();
#else
	WifiAP.stopModem();
#endif

	if (Communications.partialPacket) {
		delete Communications.partialPacket;
		Communications.partialPacket = NULL;
	}

	Communications.partialPacketTimeout = 0;
	Communications.comState				= COM_INIT;

	return COM_OK;
}

COMState CCommunications::currentState()
{
	return Communications.comState;
}

COMReturn CCommunications::addCommand(CComsCommand* command)
{
	CComsCommand** newCommands = (CComsCommand**)malloc(sizeof(CComsCommand*) * (Communications.commandCount + 1));
	if (!newCommands) {
		ESP_LOGE(COMS_TAG, "addCommand(): malloc error");
		return COM_ERROR_MALLOC;
	}

	memcpy(newCommands, Communications.commands, sizeof(CComsCommand*) * Communications.commandCount);
	newCommands[Communications.commandCount] = command;
	free(Communications.commands);
	Communications.commands = newCommands;
	Communications.commandCount++;

	ESP_LOGD(COMS_TAG, "addCommand(): added item [%u]", Communications.commandCount);

	return COM_OK;
}

COMReturn CCommunications::delCommand(uint16_t index)
{
	if (index >= Communications.commandCount) {
		ESP_LOGW(COMS_TAG, "delCommand(): invalid index");
		return COM_ERROR_NOT_FOUND;
	}

	CComsCommand** newCommands = (CComsCommand**)malloc(sizeof(CComsCommand*) * (Communications.commandCount - 1));
	if (!newCommands) {
		ESP_LOGE(COMS_TAG, "delCommand(): malloc error");
		return COM_ERROR_MALLOC;
	}

	delete Communications.commands[index];
	memcpy(newCommands, Communications.commands, sizeof(CComsCommand*) * index);
	memcpy(newCommands + index, Communications.commands + index + 1, sizeof(CComsCommand*) * (Communications.commandCount - index - 1));
	free(Communications.commands);
	Communications.commands = newCommands;
	Communications.commandCount--;

	ESP_LOGD(COMS_TAG, "delCommand(): removed item at index [%u]", index);

	return COM_OK;
}

void CCommunications::clearCommands()
{
	while (Communications.commandCount) {
		delCommand(0);
	}
}

uint16_t CCommunications::countCommand()
{
	return Communications.commandCount;
}

int32_t CCommunications::currentCommand()
{
	return Communications.commandCurrent;
}

bool CCommunications::isConnected()
{
	if (Bluetooth.isConnected() || WifiAP.isConnected()) {
		return true;
	}

	return false;
}

COMReturn CCommunications::endCurrentCommand()
{
	if (Communications.commandCurrent >= 0 && Communications.commandCurrent < Communications.commandCount) {
		CComsCommand* pCommand = Communications.commands[Communications.commandCurrent];
		ESP_LOGI(COMS_TAG, "endCurrentCommand(): ending task [%x]", pCommand->command());

		CPacket* pPacket = new CPacket();
		pCommand->end(pPacket);
		sendPacket(pPacket, pCommand->command());
		sendResponse(NULL, COM_COMMAND_END_TASK, pCommand->command());
	}

	Communications.commandCurrent = -1;

	return COM_COMPLETE;
}

void CCommunications::parseTask(void* vPtr)
{
	//subscribe to WDT
	ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
	ESP_ERROR_CHECK(esp_task_wdt_status(NULL));

	xSemaphoreTake(Communications.taskMutex, portMAX_DELAY);

	ESP_LOGI(COMS_TAG, "parseTask(): Started");
	while (Communications.comState == COM_RUN) {
		CPacket* packet = NULL;
		xQueueReceive(Communications.receiveQueue, &packet, 0);
		if (packet) {
			if (Communications.partialPacket) {
				//check and make sure we don't overrun the partial packet buffer
				if (packet->size() <= Communications.partialPacket->size()) {
					//copy packet data and then discard it
					memcpy(Communications.partialPacket->data(), packet->data(), packet->size());
					Communications.partialPacket->forward(packet->size());
					delete packet;

					//check to see if we have completed the packet
					ESP_LOGD(COMS_TAG, "parseTask(): partial left [%u]", Communications.partialPacket->size());
					if (!Communications.partialPacket->size()) {
						Communications.partialPacket->rewind();
						COMHeader* header = (COMHeader*)Communications.partialPacket->data();
						Communications.partialPacket->forward(sizeof(COMHeader));
						if (CRC32.check(Communications.partialPacket->data(), Communications.partialPacket->size(), header->crc)) {
							ESP_LOGD(COMS_TAG, "parseTask(): received [%u] bytes, calling command", Communications.partialPacket->size());
							callCommand(Communications.partialPacket);
						}
						else {
							ESP_LOGE(COMS_TAG, "parseTask(): packet failed crc check");
							sendResponse(Communications.partialPacket, COM_COMMAND_INVALID_PACKET, COM_RESPONSE_INVALID_PACKET);
						}
						Communications.partialPacket = NULL;
					}
				}
				else {
					ESP_LOGE(COMS_TAG, "parseTask(): received [%u] bytes, too large to fit in partial packet [%u]", packet->size(), Communications.partialPacket->size());

					delete Communications.partialPacket;
					Communications.partialPacket = NULL;

					sendResponse(packet, COM_COMMAND_INVALID_PACKET, COM_RESPONSE_INVALID_PACKET);
				}
			}
			else {
				//check for a valid packet { header + 2 bytes }
				if (checkPacketHeader(packet) == COM_OK) {
					COMHeader* header = (COMHeader*)packet->data();
					packet->forward(sizeof(COMHeader));
					if (packet->size() == header->size) {
						//full packet
						if (CRC32.check(packet->data(), packet->size(), header->crc)) {
							ESP_LOGD(COMS_TAG, "parseTask(): received [%u] bytes, calling command", packet->size());
							callCommand(packet);
						}
						else {
							ESP_LOGE(COMS_TAG, "parseTask(): packet failed crc check");
							sendResponse(packet, COM_COMMAND_INVALID_PACKET, COM_RESPONSE_INVALID_PACKET);
						}
					}
					else if (packet->size() < header->size) {
						//partial packet
						ESP_LOGD(COMS_TAG, "parseTask(): starting partial packet with [%u] out of [%u]", packet->size(), header->size);
						uint16_t packetSize					= packet->size();
						Communications.partialPacketTimeout = 0;
						Communications.partialPacket		= packet;
						Communications.partialPacket->addSpace(header->size - Communications.partialPacket->size());
						Communications.partialPacket->forward(packetSize);
					}
					else {
						//packet is larger than header size
						ESP_LOGE(COMS_TAG, "parseTask(): packet data longer [%u] than specified [%u]", packet->size(), header->size);
						sendResponse(packet, COM_COMMAND_INVALID_PACKET, COM_RESPONSE_INVALID_PACKET);
					}
				}
				else {
					ESP_LOGW(COMS_TAG, "parseTask(): packet header is invalid");
					sendResponse(packet, COM_COMMAND_INVALID_PACKET, COM_RESPONSE_INVALID_PACKET);
				}
			}
		}
		else {
			callIdle();
		}

		//check for partial packet timeout
		if (Communications.partialPacket) {
			Communications.partialPacketTimeout += COM_TASK_TICK_TIME;
			if (Communications.partialPacketTimeout >= COM_PACKET_TIMEOUT) {
				delete Communications.partialPacket;
				Communications.partialPacket = NULL;

				ESP_LOGD(COMS_TAG, "parseTask(): partial packet timed out");
				sendResponse(NULL, COM_COMMAND_INVALID_PACKET, COM_RESPONSE_PACKET_TIMEOUT);
			}
		}

		esp_task_wdt_reset();

		vTaskDelay(pdMS_TO_TICKS(COM_TASK_TICK_TIME));
	}

	xSemaphoreGive(Communications.taskMutex);

	//unsubscribe to WDT and deinit
	ESP_ERROR_CHECK(esp_task_wdt_delete(NULL));

	vTaskDelete(NULL);
}

void  CCommunications::dataReceived(CPacket* packet)
{
	CPacket* p = new CPacket(packet->data(), packet->size(), true);
	if (!p) {
		ESP_LOGE(COMS_TAG, "dataReceived(): malloc error");

		return;
	}

	ESP_LOGD(COMS_TAG, "dataReceived(): packet received [%u] bytes", p->size());

	if (!Communications.receiveQueue || xQueueSend(Communications.receiveQueue, &p, pdMS_TO_TICKS(TIMEOUT_NORMAL)) != pdTRUE) {
		ESP_LOGE(COMS_TAG, "dataReceived(): queue is full");

		delete p;

		return;
	}
}

COMReturn CCommunications::sendPacket(CPacket* packet)
{
	return sendPacket(packet, Communications.commandLast);
}

COMReturn CCommunications::sendPacket(CPacket* packet, uint8_t cmd)
{
	uint8_t timeout = 0;
	int     btRes	= BT_INVALID_STATE;

	//if the packet is invalid delete and abort
	if (!packet->valid()) {
		delete packet;

		return COM_ERROR_INVALID;
	}

	//add packet header
	COMHeaderCommand header;
	header.header.id	= COMS_HEADER_ID;
	header.header.size	= packet->size() + 1;
	header.header.crc	= 0;
	header.command		= cmd;
	packet->add((uint8_t*)&header, sizeof(COMHeader) + 1, 0);

	COMHeader* headerPointer = (COMHeader*)packet->data();
	packet->forward(sizeof(COMHeader));
	headerPointer->crc = CRC32.build(packet->data(), packet->size());
	packet->back(sizeof(COMHeader));

	ESP_LOGI(COMS_TAG, "sendPacket(): sending packet [%u]", packet->size());

	do {
		if (Bluetooth.isConnected()) {
			btRes = Bluetooth.send(packet);
		}
		else if (WifiAP.isConnected()) {
			btRes = WifiAP.sendData(packet);
		}

		switch (btRes) {
		case BT_OK:
			ESP_LOGD(COMS_TAG, "sendPacket(): Packet sent");
			timeout = 0;

			break;

		case BT_QUEUE_FULL:
			if (++timeout == 255) {
				ESP_LOGD(COMS_TAG, "sendPacket(): BT timeout");
				delete packet;

				return COM_ERROR;
			}
			break;

		case BT_INVALID_STATE:
			ESP_LOGD(COMS_TAG, "sendPacket(): BT not connected");
			delete packet;

			return COM_ERROR;
		}
		vTaskDelay(pdMS_TO_TICKS(10));
		esp_task_wdt_reset();
	} while (btRes != BT_OK);

	return COM_OK;
}

COMReturn CCommunications::sendResponse(CPacket* packet, uint8_t cmd, uint8_t response)
{
	if (!packet) {
		packet = new CPacket();
		if (!packet) {
			ESP_LOGE(COMS_TAG, "sendResponse(): malloc error");
			return COM_ERROR_MALLOC;
		}
	}

	ESP_LOGD(COMS_TAG, "sendResponse(): sending [%x] [%x]", cmd, response);

	packet->copy(&response, 1);
	sendPacket(packet, cmd);

	return COM_OK;
}

COMReturn CCommunications::checkPacketHeader(CPacket* packet)
{
	if (packet->size() < sizeof(COMHeader) + 2) {
		ESP_LOGD(COMS_TAG, "checkPacketHeader(): invalid size [%d]", packet->size());
		return COM_ERROR_INVALID;
	}

	COMHeader header;
	header.id = COMS_HEADER_ID;
	for (uint16_t i = 0; i < sizeof(uint16_t); i++) {
		if (packet->data()[i] != ((uint8_t*)&header.id)[i]) {
			ESP_LOGD(COMS_TAG, "checkPacketHeader(): invalid id [%d] does not match [%d]", header.id, COMS_HEADER_ID);
			return COM_ERROR_INVALID;
		}
	}

	ESP_LOGD(COMS_TAG, "checkPacketHeader(): valid header");
	return COM_OK;
}

COMReturn CCommunications::callIdle()
{
	if (Communications.commandCurrent >= 0 && Communications.commandCurrent < Communications.commandCount) {
		CComsCommand*	pCommand	= Communications.commands[Communications.commandCurrent];
		CPacket*		pPacket		= new CPacket();
		COMReturn		ret			= pCommand->idle(pPacket);
		sendPacket(pPacket);

		switch (ret) {
		case COM_OK:
			ESP_LOGD(COMS_TAG, "callIdle(): task ok");

			return COM_OK;
			break;

		case COM_WAIT:
			ESP_LOGD(COMS_TAG, "callIdle(): task wait");

			return COM_WAIT;
			break;

		case COM_COMPLETE:
			ESP_LOGI(COMS_TAG, "callIdle(): task complete");

			return endCurrentCommand();
			break;

		case COM_TIMEOUT:
			ESP_LOGI(COMS_TAG, "callIdle(): task timeout");

			endCurrentCommand();
			return COM_TIMEOUT;
			break;

		default:
			ESP_LOGI(COMS_TAG, "callIdle(): task error [%u]", ret);

			endCurrentCommand();
			return COM_ERROR;
			break;
		}
	}

	return endCurrentCommand();
}

COMReturn CCommunications::callCommand(CPacket* packet)
{
	if (!Communications.commandCount) {
		delete packet;

		return COM_ERROR;
	}

	//if we aren't currently running a command lets see if we can start one
	if (Communications.commandCurrent < 0) {
		//if the packet has a valid start command lets try to locate the correct task
		if (packet->data()[0] == COM_COMMAND_START_TASK) {
			for(uint16_t i = 0; i < Communications.commandCount; i++) {
				CComsCommand* pCommand = Communications.commands[i];
				if (pCommand->command() == packet->data()[1]) {
					packet->forward(2);
					if (pCommand->start(packet) == COM_OK) {
						Communications.commandCurrent	= i;
						Communications.commandLast		= pCommand->command();

						ESP_LOGI(COMS_TAG, "callCommand(): starting command [%x]", pCommand->command());
						sendResponse(NULL, COM_COMMAND_START_TASK, pCommand->command());
						sendPacket(packet);

						return COM_OK;
					}
					else {
						ESP_LOGI(COMS_TAG, "callCommand(): task failed to start [%x]", pCommand->command());
						sendResponse(packet, COM_COMMAND_START_TASK, COM_RESPONSE_TASK_NOT_STARTED);

						return COM_ERROR;
					}
				}
			}

			//if we did not successfully start a command lets abort
			ESP_LOGI(COMS_TAG, "callCommand(): unable to find task [%x]", packet->data()[1]);
			sendResponse(packet, COM_COMMAND_START_TASK, COM_RESPONSE_INVALID_COMMAND);

			return COM_ERROR_INVALID;
		}
		else {
			//if packet wasn't set to start a task lets notify the client
			ESP_LOGI(COMS_TAG, "callCommand(): task not started [%x]", packet->data()[1]);
			sendResponse(packet, packet->data()[1], COM_RESPONSE_TASK_NOT_STARTED);

			return COM_ERROR_INVALID;
		}
	}

	//if we have a command running lets send it the packet
	if (Communications.commandCurrent < Communications.commandCount) {
		CComsCommand*	pCommand	= Communications.commands[Communications.commandCurrent];

		//check to see if we received an internal command or a user defined command
		switch (packet->data()[0]) {
		case COM_COMMAND_START_TASK:
			ESP_LOGI(COMS_TAG, "callCommand(): task already running [%x]", packet->data()[1]);
			sendResponse(packet, COM_COMMAND_START_TASK, COM_RESPONSE_TASK_ALREADY_RUNNING);
			return COM_OK;
			break;

		case COM_COMMAND_CURRENT_TASK:
			ESP_LOGI(COMS_TAG, "callCommand(): sending current task [%x]", pCommand->command());
			sendResponse(packet, COM_COMMAND_CURRENT_TASK, pCommand->command());
			return COM_OK;
			break;

		case COM_COMMAND_END_TASK:
			ESP_LOGI(COMS_TAG, "callCommand(): ending current task [%x]", pCommand->command());
			pCommand->end(packet);
			sendPacket(packet);
			sendResponse(NULL, COM_COMMAND_END_TASK, pCommand->command());
			Communications.commandCurrent = -1;
			return COM_COMPLETE;
			break;

		default:
			Communications.commandLast = packet->data()[0];

			ESP_LOGD(COMS_TAG, "callCommand(): running current task [%x]", pCommand->command());
			COMReturn ret = pCommand->receive(packet);

			switch (ret) {
			case COM_OK:
				ESP_LOGD(COMS_TAG, "callCommand(): task ok");

				if (!packet->size()) {
					sendResponse(packet, pCommand->command(), COM_RESPONSE_OK);
				}
				else {
					sendPacket(packet);
				}
				return COM_OK;
				break;

			case COM_WAIT:
				ESP_LOGD(COMS_TAG, "callCommand(): task wait");

				if (!packet->size()) {
					sendResponse(packet, pCommand->command(), COM_RESPONSE_WAIT);
				}
				else {
					sendPacket(packet);
				}
				return COM_WAIT;
				break;

			case COM_COMPLETE:
				ESP_LOGI(COMS_TAG, "callCommand(): task complete");

				sendPacket(packet);
				return endCurrentCommand();
				break;

			case COM_TIMEOUT:
				ESP_LOGI(COMS_TAG, "callCommand(): task timeout");

				sendPacket(packet);
				endCurrentCommand();
				return COM_TIMEOUT;
				break;

			default:
				ESP_LOGI(COMS_TAG, "callCommand(): task error [%u]", ret);

				sendPacket(packet);
				endCurrentCommand();
				return COM_ERROR;
				break;
			}
		}
	}

	delete packet;

	return endCurrentCommand();
}