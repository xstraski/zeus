#ifndef GAME_PLATFORM_LINUX_H
#define GAME_PLATFORM_LINUX_H

#include "game_platform.h"

// POSIX standard includes.
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <dlfcn.h>

// POSIX threads includes.
#include <pthread.h>
#include <semaphore.h>

// X11 includes.
#include <X11/Xlib.h>
#include <X11/Xos.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XShm.h>

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
	sem_t Semaphore;

	work_queue_startup *Startups;
	work_queue_entry Entries[256];
};

inline struct timespec
LinuxGetClock(void)
{
	struct timespec Clock;
	clock_gettime(CLOCK_MONOTONIC, &Clock);
	return Clock;
}

inline f32
LinuxGetSecondsElapsed(struct timespec Start, struct timespec End)
{
	return ((f32)(End.tv_sec - Start.tv_sec) + ((f32)(End.tv_nsec - Start.tv_nsec) * 1e-9f));
}

// NOTE(ivan): X11 window client geoemtry.
struct linux_window_client_dimension {
	s32 Width;
	s32 Height;
};

inline linux_window_client_dimension
LinuxGetWindowClientDimension(Display *XDisplay,
							  Window Wind)
{
	linux_window_client_dimension Result;

	Window RootWindow;
	int WindowX, WindowY;
	unsigned int WindowWidth, WindowHeight;
	unsigned int WindowBorder, WindowDepth;
	XGetGeometry(XDisplay,
				 Wind,
				 &RootWindow,
				 &WindowX, &WindowY,
				 &WindowWidth, &WindowHeight,
				 &WindowBorder, &WindowDepth);

	Result.Width = WindowWidth;
	Result.Height = WindowHeight;

	return Result;
}

// NOTE(ivan): Linux surface buffer.
struct linux_surface_buffer {
	XImage *Image;
	XShmSegmentInfo SegmentInfo;

	void *Pixels;
	s32 Width;
	s32 Height;
	s32 BytesPerPixel;
	s32 Pitch;
};

// NOTE(ivan): Linux layer state structure.
struct platform_state {
	s32 NumParams;
	char **Params;

	b32 Running;

	char ExeName[1024];
	char ExeNameNoExt[1024];
	char ExePath[1024];

	work_queue HighPriorityWorkQueue;
	work_queue LowPriorityWorkQueue;

	// NOTE(ivan): X server stuff.
	Display *XDisplay;
	int XScreen;
	int XDepth;
	unsigned long XBlack;
	unsigned long XWhite;
	Window XRootWindow;
	Visual *XVisual;
	Atom WMDeleteWindow;
	
	Window MainWindow;
	GC MainWindowGC;

	// NOTE(ivan): Game entities module information.
	void * EntitiesLibrary;
	ino_t EntitiesLibraryLastId;

	linux_surface_buffer SurfaceBuffer;
};

PLATFORM_CHECK_PARAM(LinuxCheckParam);
PLATFORM_CHECK_PARAM_VALUE(LinuxCheckParamValue);
PLATFORM_LOG(LinuxLog);
PLATFORM_ERROR(LinuxError);
PLATFORM_ALLOCATE_MEMORY(LinuxAllocateMemory);
PLATFORM_DEALLOCATE_MEMORY(LinuxDeallocateMemory);
PLATFORM_GET_MEMORY_STATS(LinuxGetMemoryStats);
PLATFORM_ADD_WORK_QUEUE_ENTRY(LinuxAddWorkQueueEntry);
PLATFORM_COMPLETE_WORK_QUEUE(LinuxCompleteWorkQueue);
PLATFORM_READ_ENTIRE_FILE(LinuxReadEntireFile);
PLATFORM_FREE_ENTIRE_FILE_MEMORY(LinuxFreeEntireFileMemory);
PLATFORM_WRITE_ENTIRE_FILE(LinuxWriteEntireFile);
PLATFORM_RELOAD_ENTITIES_MODULE(LinuxReloadEntitiesModule);
PLATFORM_SHOULD_ENTITIES_MODULE_BE_RELOADED(LinuxShouldEntitiesModuleBeReloaded);

#endif // #ifndef GAME_PLATFORM_LINUX_H
