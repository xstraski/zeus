#ifndef WIN32_ZEUS_H
#define WIN32_ZEUS_H

#include "zeus_platform.h"

// NOTE(ivan): Compiler check.
#if !MSVC
#error "Unsupported compiler for Windows target platform!"
#endif

// NOTE(ivan): Windows versions definitions.
#include <sdkddkver.h>
#undef _WIN32_WINNT
#undef _WIN32_IE
#undef NTDDI_VERSION

// NOTE(ivan): Set target Windows version: Windows 7.
#define _WIN32_WINNT _WIN32_WINNT_WIN7
#define _WIN32_IE _WIN32_IE_WIN7
#define NTDDI_VERSION NTDDI_VERSION_FROM_WIN32_WINNT(_WIN32_WINNT)

// NOTE(ivan): Strict type mode enable.
//#define STRICT

// Exclude rarely-used crap from windows headers:
//
// WIN32_LEAN_AND_MEAN:          keep the api header being a minimal set of mostly-required declarations and includes
// OEMRESOURCE:                  exclude OEM resource values (dunno wtf is that, but never needed them...)
// NOATOM:                       exclude atoms and their api (barely used today obsolete technique of pooling strings)
// NODRAWTEXT:                   exclude DrawText() and DT_* definitions
// NOMETAFILE:                   exclude METAFILEPICT (yet another windows weirdo we don't need)
// NOMINMAX:                     exclude min() & max() macros (we have our own)
// NOOPENFILE:                   exclude OpenFile(), OemToAnsi(), AnsiToOem(), and OF_* definitions (useless for us)
// NOSCROLL:                     exclude SB_* definitions and scrolling routines
// NOSERVICE:                    exclude Service Controller routines, SERVICE_* equates, etc...
// NOSOUND:                      exclude sound driver routines (we'd rather use OpenAL or another thirdparty API)
// NOTEXTMETRIC:                 exclude TEXTMETRIC and associated routines
// NOWH:                         exclude SetWindowsHook() and WH_* defnitions
// NOCOMM:                       exclude COMM driver routines
// NOKANJI:                      exclude Kanji support stuff
// NOHELP:                       exclude help engine interface
// NOPROFILER:                   exclude profiler interface
// NODEFERWINDOWPOS:             exclude DeferWindowPos() routines
// NOMCX:                        exclude Modem Configuration Extensions (modems in 2018, really?)
#define WIN32_LEAN_AND_MEAN
#define OEMRESOURCE
#define NOATOM
#define NODRAWTEXT
#define NOMETAFILE
#define NOMINMAX
#define NOOPENFILE
#define NOSCROLL
#define NOSERVICE
#define NOSOUND
#define NOTEXTMETRIC
#define NOWH
#define NOCOMM
#define NOKANJI
#define NOHELP
#define NOPROFILER
#define NODEFERWINDOWPOS
#define NOMCX

// NOTE(ivan): DirectInput version.
#define DIRECTINPUT_VERSION 0x0800

// NOTE(ivan): The main headers.
#include <windows.h>
#include <mmsystem.h>
#include <xinput.h>

// NOTE(ivan): Undefine unwanted Windows definitions.
#undef YieldProcessor
#undef BitScanForward
#undef BitScanReverse

// NOTE(ivan): Release macro for COM objects.
#define ReleaseCOM(Obj) do {if (Obj) Obj->Release(); Obj = 0;} while(0)

// NOTE(ivan): Benchmarking functions.
inline u64
Win32GetClock(void)
{
	LARGE_INTEGER PerformanceCounter;
	QueryPerformanceCounter(&PerformanceCounter);

	return PerformanceCounter.QuadPart;
}
inline f64
Win32GetSecondsElapsed(u64 Start, u64 End, u64 Frequency)
{
	u64 Diff = End - Start;
	return (f64)(Diff / (f64)Frequency);
}

// NOTE(ivan): Work queue entry structure.
struct work_queue_entry {
	work_queue_callback *Callback;
	void *Data;
};

// NOTE(ivan): Work queue thread startup parameters structure.
struct work_queue_startup {
	work_queue *Queue;
};

// NOTE(ivan): Work queue implementation.
struct work_queue {
	volatile u32 CompletionGoal;
	volatile u32 CompletionCount;

	volatile u32 NextEntryToWrite;
	volatile u32 NextEntryToRead;
	HANDLE Semaphore;

	work_queue_startup *Startups;
	work_queue_entry Entries[256];
};

// NOTE(ivan): Platform-specific window dimensions.
struct win32_window_dimension {
	s32 Width;
	s32 Height;
};

// NOTE(ivan): XInput prototypes.
#define X_INPUT_GET_STATE(name) DWORD WINAPI name(DWORD UserIndex, XINPUT_STATE *State)
typedef X_INPUT_GET_STATE(x_input_get_state);

#define X_INPUT_SET_STATE(name) DWORD WINAPI name(DWORD UserIndex, XINPUT_VIBRATION *Vibration)
typedef X_INPUT_SET_STATE(x_input_set_state);

// NOTE(ivan): XInput access structure.
struct x_input {
	HMODULE Library;

	x_input_get_state *GetState;
	x_input_set_state *SetState;
};

inline win32_window_dimension
Win32GetWindowDimension(HWND Window)
{
	RECT Rect;
	GetClientRect(Window, &Rect);

	win32_window_dimension Result;
	Result.Width = Rect.right - Rect.left;
	Result.Height = Rect.bottom - Rect.top;

	return Result;
}

// NOTE(ivan): Platform-specific screen buffer structure.
struct win32_surface_buffer {
	void *Pixels;
	BITMAPINFO Info;
	s32 Width;
	s32 Height;
	s32 BytesPerPixel;
	s32 Pitch;
};

// NOTE(ivan): Platform-specific state structure implementation.
struct platform_state {
	HINSTANCE Instance;
	int ShowCommand;

	b32 Running;

	// NOTE(ivan): Command line parameters:
	char **Params;
	s32 NumParams;

	UINT QueryCancelAutoplay; // NOTE(ivan): Window message for suppressing CD-ROM autoplay event.

	char ExeName[MAX_PATH + 1];
	char ExeNameNoExt[MAX_PATH + 1];
	char ExePath[MAX_PATH + 1];

	HANDLE LogFile;

	u64 PerformanceFrequency;

	ticket_mutex MemoryMutex; // NOTE(ivan): Mutex for Win32AllocateMemory()/Win32DeallocateMemory().

	work_queue HighPriorityWorkQueue;
	work_queue LowPriorityWorkQueue;
	
	HWND MainWindow;
	HDC  MainWindowDC;
	WINDOWPLACEMENT MainWindowPlacement;

	b32 DebugCursor; // NOTE(ivan): Whether to show the cross cursor upon main window or not.

	// NOTE(ivan): Off-screen graphics back buffer.
	win32_surface_buffer SurfaceBuffer;

	// NOTE(ivan): Input stuff.
	x_input XInput;
};

PLATFORM_CHECK_PARAM(Win32CheckParam);
PLATFORM_CHECK_PARAM_VALUE(Win32CheckParamValue);
PLATFORM_LOG(Win32Log);
PLATFORM_ERROR(Win32Error);
PLATFORM_ALLOCATE_MEMORY(Win32AllocateMemory);
PLATFORM_DEALLOCATE_MEMORY(Win32DeallocateMemory);
PLATFORM_GET_MEMORY_STATS(Win32GetMemoryStats);
PLATFORM_ADD_WORK_QUEUE_ENTRY(Win32AddWorkQueueEntry);
PLATFORM_COMPLETE_WORK_QUEUE(Win32CompleteWorkQueue);
PLATFORM_READ_ENTIRE_FILE(Win32ReadEntireFile);
PLATFORM_FREE_ENTIRE_FILE_MEMORY(Win32FreeEntireFileMemory);
PLATFORM_WRITE_ENTIRE_FILE(Win32WriteEntireFile);

#endif // #ifndef WIN32_ZEUS_H
