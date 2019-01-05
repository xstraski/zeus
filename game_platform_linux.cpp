#include "game.h"
#include "game_platform.h"
#include "game_platform_linux.h"

#include "ents.h"

PLATFORM_CHECK_PARAM(LinuxCheckParam)
{
	Assert(PlatformState);
	Assert(Param);
	
	for (s32 Index = 0; Index < PlatformState->NumParams; Index++) {
		if (strcmp(PlatformState->Params[Index], Param) == 0)
			return Index;
	}

	return -1;
}

PLATFORM_CHECK_PARAM_VALUE(LinuxCheckParamValue)
{
	Assert(PlatformState);
	Assert(Param);
	
	s32 Index = LinuxCheckParam(PlatformState, Param);
	if (Index == -1)
		return 0;

	if ((Index + 1) >= PlatformState->NumParams)
		return 0;

	return PlatformState->Params[Index + 1];
}

PLATFORM_LOG(LinuxLog)
{
	Assert(PlatformState);
	Assert(MessageFormat);

#if INTERNAL	
	static ticket_mutex LogMutex = {};
	EnterTicketMutex(&LogMutex);

	char MessageBuffer[2048];
	CollectArgsN(MessageBuffer, CountOf(MessageBuffer), MessageFormat);

	char FinalMessage[2170];
	snprintf(FinalMessage, CountOf(FinalMessage), "%s\r\n", MessageBuffer);

	printf("%s", FinalMessage);
	
	LeaveTicketMutex(&LogMutex);
#endif	
}

PLATFORM_ERROR(LinuxError)
{
	Assert(PlatformState);
	Assert(ErrorFormat);

#if INTERNAL	
	static ticket_mutex ErrorMutex = {};
	EnterTicketMutex(&ErrorMutex);
	
	static b32 InError = false;
	if (InError) {
		LeaveTicketMutex(&ErrorMutex);
		exit(0);
	}
	InError = true;
	
	char ErrorBuffer[2048];
	CollectArgsN(ErrorBuffer, CountOf(ErrorBuffer), ErrorFormat);

	LinuxLog(PlatformState, "*** ERROR *** %s", ErrorBuffer);

	LeaveTicketMutex(&ErrorMutex);
#endif	
	exit(0);
}

// TODO(ivan): Implement a reliable Linux memory allocator.
PLATFORM_ALLOCATE_MEMORY(LinuxAllocateMemory)
{
	Assert(Bytes);
	return malloc(Bytes);
}

// TODO(ivan): Implement a reliable Linux memory allocator.
PLATFORM_DEALLOCATE_MEMORY(LinuxDeallocateMemory)
{
	if (!Address)
		return;
	free(Address);
}

PLATFORM_GET_MEMORY_STATS(LinuxGetMemoryStats)
{
	platform_memory_stats Result = {};

	Result.BytesTotal = (u64)sysconf(_SC_PHYS_PAGES) * (u64)sysconf(_SC_PAGE_SIZE);
	Result.BytesAvailable = (u64)sysconf(_SC_AVPHYS_PAGES) * (u64)sysconf(_SC_PAGE_SIZE);

	return Result;
}

static b32
LinuxDoNextWorkQueueEntry(work_queue *Queue)
{
	Assert(Queue);

	b32 ShouldSleep = false;

	u32 OrigNextEntryToRead = Queue->NextEntryToRead;
	u32 NewNextEntryToRead = (OrigNextEntryToRead + 1) % CountOf(Queue->Entries);
	if (OrigNextEntryToRead != Queue->NextEntryToWrite) {
		u32 Index = AtomicCompareExchangeU32(&Queue->NextEntryToRead,
											 NewNextEntryToRead,
											 OrigNextEntryToRead);
		if (Index == OrigNextEntryToRead) {
			work_queue_entry Entry = Queue->Entries[Index];
			Entry.Callback(Queue, Entry.Data);
			AtomicIncrementU32(&Queue->CompletionCount);
		}
	} else {
		ShouldSleep = true;
	}

	return ShouldSleep;
}

static void *
LinuxWorkQueueProc(void *Param)
{
	work_queue_startup *Startup = (work_queue_startup *)Param;
	work_queue *Queue = Startup->Queue;

	while (true) {
		if (LinuxDoNextWorkQueueEntry(Queue))
			sem_wait(&Queue->Semaphore);
	}
}

static void
LinuxInitializeWorkQueue(platform_state *PlatformState,
						 work_queue *Queue,
						 u32 ThreadCount)
{
	Assert(PlatformState);
	Assert(Queue);
	Assert(ThreadCount);

	Queue->CompletionGoal = Queue->CompletionCount = 0;
	Queue->NextEntryToWrite = Queue->NextEntryToRead = 0;

	u32 InitialCount = 0;
	sem_init(&Queue->Semaphore, 0, InitialCount);

	Queue->Startups = (work_queue_startup *)LinuxAllocateMemory(sizeof(work_queue_startup) * ThreadCount);
	if (!Queue->Startups)
		LinuxError(PlatformState, "Failed allocating resources for work queue!");
	for (u32 ThreadIndex = 0; ThreadIndex < ThreadCount; ThreadIndex++) {
		Queue->Startups[ThreadIndex].Queue = Queue;

		pthread_attr_t ThreadAttr;
		pthread_attr_init(&ThreadAttr);
		pthread_attr_setdetachstate(&ThreadAttr, PTHREAD_CREATE_DETACHED);

		pthread_t Thread;
		int Result = pthread_create(&Thread, &ThreadAttr, LinuxWorkQueueProc, &Queue->Startups[ThreadIndex]);
		pthread_attr_destroy(&ThreadAttr);
	}
}

static void
LinuxReleaseWorkQueue(work_queue *Queue)
{
	Assert(Queue);
	LinuxDeallocateMemory(Queue->Startups);
}

PLATFORM_ADD_WORK_QUEUE_ENTRY(LinuxAddWorkQueueEntry)
{
	Assert(Queue);
	Assert(Callback);
	
	// TODO(ivan): Switch to AtomicCompareExchange() eventually so that any thread can add?
	u32 NewNextEntryToWrite = (Queue->NextEntryToWrite + 1) % CountOf(Queue->Entries);
	Assert(NewNextEntryToWrite != Queue->NextEntryToRead);

	work_queue_entry *Entry = Queue->Entries + Queue->NextEntryToWrite;
	Entry->Callback = Callback;
	Entry->Data = Data;
	Queue->CompletionGoal++;
	CompletePastWritesBeforeFutureWrites();

	Queue->NextEntryToWrite = NewNextEntryToWrite;
	sem_post(&Queue->Semaphore);
}

PLATFORM_COMPLETE_WORK_QUEUE(LinuxCompleteWorkQueue)
{
	while (Queue->CompletionGoal != Queue->CompletionCount)
		LinuxDoNextWorkQueueEntry(Queue);

	Queue->CompletionGoal = Queue->CompletionCount = 0;
}

PLATFORM_READ_ENTIRE_FILE(LinuxReadEntireFile)
{
	Assert(FileName);

	piece Result = {};

	int File = open(FileName, O_RDONLY);
	if (File != -1) {
		off_t FileSize64 = lseek(File, 0, SEEK_END);
		lseek(File, 0, SEEK_SET);
		
		if (FileSize64 > 0) {
			u32 FileSize = SafeTruncateU64(FileSize64);
			Result.Bytes = FileSize;
			Result.Memory = (u8 *)LinuxAllocateMemory(FileSize);
			if (Result.Memory) {
				if (read(File, Result.Memory, FileSize) == FileSize) {
					// NOTE(ivan): Success.
				} else {
					LinuxDeallocateMemory(Result.Memory);
					Result.Memory = 0;
				}
			}
		}

		close(File);
	}
	
	return Result;
}

PLATFORM_FREE_ENTIRE_FILE_MEMORY(LinuxFreeEntireFileMemory)
{
	Assert(ReadResult);
	Assert(ReadResult->Memory);
	
	LinuxDeallocateMemory(ReadResult->Memory);
}

PLATFORM_WRITE_ENTIRE_FILE(LinuxWriteEntireFile)
{
	Assert(FileName);
	Assert(Memory);
	Assert(Bytes);

	b32 Result = true;
	
	int File = open(FileName, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
	if (File != -1) {
		ssize_t BytesWritten = write(File, Memory, Bytes);
		if (fsync(File) >= 0)
			Result = (BytesWritten == (ssize_t)Bytes);
		else
			Result = false;
		
		close(File);
	}
	
	return Result;
}

static void
LinuxCopyFile(const char *FileName, const char *NewName)
{
	Assert(FileName);
	Assert(NewName);

	int FileTo;
	int FileFrom;
	off_t Size64;
	u32 Size;
	u8 *Buffer;
	ssize_t NumRead;
	ssize_t NumWritten;

	FileFrom = open(FileName, O_RDONLY);
	if (FileFrom == -1)
		goto OutError;

	FileTo = open(NewName, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
	if (FileTo == -1)
		goto OutError;

	Size64 = lseek(FileFrom, 0, SEEK_END);
	lseek(FileFrom, 0, SEEK_SET);
	Size = SafeTruncateU64(Size64);

	Buffer = (u8 *)LinuxAllocateMemory(Size);
	if (!Buffer)
		goto OutError;

	NumRead = read(FileFrom, Buffer, Size);
	if (NumRead != Size)
		goto OutError;

	NumWritten = write(FileTo, Buffer, NumRead);
	if (NumWritten != NumRead)
		goto OutError;

 OutError:
	if (Buffer)
		LinuxDeallocateMemory(Buffer);
	if (FileTo != -1)
		close(FileTo);
	if (FileFrom != -1)
		close(FileFrom);
}

PLATFORM_RELOAD_ENTITIES_MODULE(LinuxReloadEntitiesModule)
{
	Assert(PlatformState);
	Assert(FileName);
	Assert(TempFileName);
	Assert(GameAPI);

	if (PlatformState->EntitiesLibrary) {
		dlclose(PlatformState->EntitiesLibrary);
		PlatformState->EntitiesLibrary = 0;
	}
	
	LinuxCopyFile(FileName, TempFileName);
	
	PlatformState->EntitiesLibrary = dlopen(TempFileName, RTLD_NOW);
	if (!PlatformState->EntitiesLibrary)
		LinuxError(PlatformState, "Failed loading entities module!");

	struct stat EntitiesLibraryStat;
	lstat(FileName, &EntitiesLibraryStat);
	PlatformState->EntitiesLibraryLastId = EntitiesLibraryStat.st_ino;
	
	register_all_entities *RegisterAllEntities = (register_all_entities *)dlsym(PlatformState->EntitiesLibrary,
																				"RegisterAllEntities");
	if (!RegisterAllEntities)
		LinuxError(PlatformState, "Failed verifying entities module!");
	
	RegisterAllEntities(GameAPI);
}

PLATFORM_SHOULD_ENTITIES_MODULE_BE_RELOADED(LinuxShouldEntitiesModuleBeReloaded)
{
	Assert(PlatformState);
	Assert(FileName);

	struct stat EntitiesLibraryStat;
	lstat(FileName, &EntitiesLibraryStat);
	ino_t CurrentId = EntitiesLibraryStat.st_ino;
	
	if (CurrentId != PlatformState->EntitiesLibraryLastId)
		return true;

	return false;
}

int
main(int NumParams, char **Params)
{
	platform_state PlatformState = {};
	
	PlatformState.NumParams = NumParams;
	PlatformState.Params = Params;

	// NOTE(ivan): Connect to X server.
	PlatformState.XDisplay = XOpenDisplay(getenv("DISPLAY"));
	if (!PlatformState.XDisplay) {
		if (getenv("DISPLAY"))
			LinuxError(&PlatformState, "Cannot connect to X display [:%s]!", getenv("DISPLAY"));
		else
			LinuxError(&PlatformState, "Cannot connect to X display!");
	}

	// NOTE(ivan): Disconnect from X server.
	if (PlatformState.XDisplay)
		XCloseDisplay(PlatformState.XDisplay);
	
	return 0;
}
