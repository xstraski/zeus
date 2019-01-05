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
#include <dlfcn.h>

// POSIX threads includes.
#include <pthread.h>
#include <semaphore.h>

// X11 includes.
#include <X11/Xlib.h>
#include <X11/Xos.h>
#include <X11/Xutil.h>
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

// NOTE(ivan): Linux layer state structure.
struct platform_state {
	s32 NumParams;
	char **Params;

	// NOTE(ivan): X server stuff.
	Display *XDisplay;

	// NOTE(ivan): Game entities module information.
	void * EntitiesLibrary;
	ino_t EntitiesLibraryLastId;
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
