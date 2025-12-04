#include "log.h"

int log_init(void)
{
    SEGGER_RTT_Init();
    return 0;
}


