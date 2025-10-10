/* date = June 17th 2025 3:06 pm */

#ifndef LINUX_PROFILE_H
#define LINUX_PROFILE_H

#if HANDMADE_PROFILING

#include "handmade_platform.h"

#include "spall.h"
global_variable SpallProfile spall_ctx;
global_variable SpallBuffer spall_buffer;
global_variable char SpallNumberBuffer[9];

#define PROFILE_START_LABEL(Label) \
spall_buffer_begin(&spall_ctx, &spall_buffer, Label, strlen(Label), LinuxGetWallClockMSec())
#define PROFILE_START_LINE sprintf(SpallNumberBuffer, "%d", __LINE__); \
PROFILE_START_LABEL(SpallNumberBuffer)
#define PROFILE_START_FUNCTION PROFILE_START_LABEL(__FUNCTION__)
#define PROFILE_START PROFILE_START_LINE
#define PROFILE_END spall_buffer_end(&spall_ctx, &spall_buffer, LinuxGetWallClockMSec());

typedef struct timespec platform_get_wall_clock(void);

global_variable platform_get_wall_clock *PlatformGetWallClock;
timespec LinuxGetWallClock(void);

internal 
r64 LinuxGetWallClockMSec(void)
{
	r64 Result = 0;
    timespec Spec = PlatformGetWallClock();
    Result = (((r64)Spec.tv_sec) * 1000000) + (((r64)Spec.tv_nsec) / 1000);
    return Result;
}

#else
#define PROFILE_START
#define PROFILE_START_LABEL(Name)
#define PROFILE_START_LINE
#define PROFILE_START_FUNCTION
#define PROFILE_END
#endif

#endif //LINUX_PROFILE_H
