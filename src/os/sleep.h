/** @file src/os/sleep.h OS-independent inclusion of the delay routine. */

#ifndef OS_SLEEP_H
#define OS_SLEEP_H

#if defined(_WIN32)
	#include <windows.h>
	#define sleep(x) Sleep(x * 1000)
	#define msleep(x) Sleep(x)
#else
	#if !defined(__USE_BSD)
		#define __USE_BSD
		#include <unistd.h>
		#undef __USE_BSD
	#else
		#include <unistd.h>
	#endif /* __USE_BSD */

	#define msleep(x) usleep(x * 1000)
#endif /* _WIN32 */

#include "../timer.h"

#if defined(_WIN32) && !defined(WITH_SDL) && !defined(WITH_SDL2)
/* Timer_Tick()/Video_Tick() etc. still tick via the separate Windows
 * timer-queue thread on this config (see timer.c) -- this only runs
 * idle hooks (Timer_AddIdleHook()), for things that must stay on
 * whichever thread actually calls sleepIdle(). */
#define sleepIdle() (msleep(1), Timer_RunIdleHooks())
#else /* _WIN32 */
#define sleepIdle SleepAndProcessBackgroundTasks
#endif /* _WIN32 */

#endif /* OS_SLEEP_H */
