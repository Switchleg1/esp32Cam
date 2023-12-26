#include "currenttime.h"

CCurrentTime CurrentTime;

uint32_t CCurrentTime::us() {
	return esp_timer_get_time();
}

uint32_t CCurrentTime::ms() {
	return esp_timer_get_time() / 1000;
}

uint32_t CCurrentTime::s() {
	return esp_timer_get_time() / 1000000;
}