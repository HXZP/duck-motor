#ifndef __PT_SEM_H__
#define __PT_SEM_H__

#include "pt.h"

struct pt_sem
{
    unsigned int count;
};

#define PT_SEM_INIT(s, c) (s)->count = (c)

#define PT_SEM_WAIT(pt, s) \
    do \
    { \
        PT_WAIT_UNTIL((pt), (s)->count > 0U); \
        --(s)->count; \
    } while (0)

#define PT_SEM_SIGNAL(pt, s) ++(s)->count

#endif
