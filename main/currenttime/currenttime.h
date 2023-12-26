#ifndef CURRENT_TIME_H
#define CURRENT_TIME_H

#include "globals.h"

class CCurrentTime {
public:
	uint32_t	us();
	uint32_t	ms();
	uint32_t	s();
};

extern CCurrentTime CurrentTime;

#endif