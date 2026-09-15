/** @file src/timer.h Timer definitions. */

#ifndef TIMER_H
#define TIMER_H

typedef enum TimerType {
	TIMER_GUI  = 1,                                         /*!< The identifier for GUI timer. */
	TIMER_GAME = 2                                          /*!< The identifier for Game timer. */
} TimerType;

extern volatile uint32 g_timerGUI;
extern volatile uint32 g_timerGame;
extern volatile uint32 g_timerInput;
extern volatile uint32 g_timerSleep;
extern volatile uint32 g_timerTimeout;

extern uint32 Timer_GetTime(void);

extern void Timer_Sleep(uint16 ticks);
extern bool Timer_SetTimer(TimerType timer, bool set);

extern void Timer_Init(void);
extern void Timer_Uninit(void);

extern void Timer_Tick(void);

extern void Timer_Add(void (*callback)(void), uint32 usec_delay, bool callonce);
extern void Timer_Change(void (*callback)(void), uint32 usec_delay);
extern void Timer_Remove(void (*callback)(void));

/* Idle hooks: callbacks run synchronously, inline, on whatever thread calls
 * SleepAndProcessBackgroundTasks()/sleepIdle() -- unlike Timer_Add(), which
 * on Win32 without SDL/SDL2 runs callbacks on a separate Windows timer-queue
 * thread (see timer.c). Use this instead of Timer_Add() for anything that
 * must never run on that separate thread (e.g. WinMM calls, which can
 * deadlock if the thread they'd normally run on gets SuspendThread()'d
 * mid-call by that mechanism -- see adl_music_win32.cpp). Cheap/no-op to
 * register on platforms where sleepIdle() already drives
 * Timer_InterruptRun() itself. */
extern void Timer_AddIdleHook(void (*callback)(void));
extern void Timer_RemoveIdleHook(void (*callback)(void));
extern void Timer_RunIdleHooks(void);

#if !defined(_WIN32) || defined(WITH_SDL) || defined(WITH_SDL2)
extern void SleepAndProcessBackgroundTasks(void);
#endif /* !_WIN32 || SDL || SDL2 */

#endif /* TIMER_H */
