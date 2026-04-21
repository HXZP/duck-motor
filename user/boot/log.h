#ifndef __LOG_H__
#define __LOG_H__

#include <stdio.h>
#include <string.h>
#include "rtt/SEGGER_RTT.h"

#define LOG_ENABLE 1

#if LOG_ENABLE
#define printf(...) SEGGER_RTT_printf(0, __VA_ARGS__)
#else
#define printf(...)
#endif

int log_init(void);






#endif

