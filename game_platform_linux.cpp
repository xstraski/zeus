#include "game.h"
#include "game_platform.h"
#include "game_platform_linux.h"

#include "ents.h"

// NOTE(ivan): Default values of main window geometry.
#define DEF_WINDOW_X 20
#define DEF_WINDOW_Y 20
#define DEF_WINDOW_WIDTH 1024
#define DEF_WINDOW_HEIGHT 768

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

	int FromFile = open(FileName, O_RDONLY);
	if (FromFile != -1) {
		off_t Size64 = lseek(FromFile, 0, SEEK_END);
		lseek(FromFile, 0, SEEK_SET);
		u32 Size = SafeTruncateU64(Size64);
		if (Size) {
			void *Buffer = LinuxAllocateMemory(Size);
			if (Buffer) {
				ssize_t NumRead = read(FromFile, Buffer, Size);
				if (NumRead == Size) {
					int ToFile = open(NewName, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
					if (ToFile != -1) {
						ssize_t NumWritten = write(ToFile, Buffer, Size);
						if (NumWritten == Size) {
							// NOTE(ivan): Success.
						}
						
						close(ToFile);
					}
				}
				
				LinuxDeallocateMemory(Buffer);
			}
		}

		close(FromFile);
	}
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

static void
LinuxResizeSurfaceBuffer(platform_state *PlatformState,
						 linux_surface_buffer *Buffer,
						 s32 NewWidth, s32 NewHeight)
{
	Assert(PlatformState);
	Assert(Buffer);

	if (Buffer->Image) {
		XShmDetach(PlatformState->XDisplay,
				   &Buffer->SegmentInfo);
		XDestroyImage(Buffer->Image);
		shmdt(Buffer->SegmentInfo.shmaddr);
		shmctl(Buffer->SegmentInfo.shmid, IPC_RMID, 0);

		LinuxDeallocateMemory(Buffer->Pixels);
		
		Buffer->Image = 0;
		Buffer->Pixels = 0;
	}

	static const s32 BytesPerPixel = 4; // NOTE(ivan): Hardcoded 32-bit color.

	if ((NewWidth * NewHeight) != 0) {
		Buffer->Image = XShmCreateImage(PlatformState->XDisplay,
										PlatformState->XVisual,
										PlatformState->XDepth,
										ZPixmap,
										0,
										&Buffer->SegmentInfo,
										NewWidth, NewHeight);
		Buffer->SegmentInfo.shmid = shmget(IPC_PRIVATE,
										   Buffer->Image->bytes_per_line * Buffer->Image->height,
										   IPC_CREAT | 0777);
		Buffer->SegmentInfo.shmaddr = Buffer->Image->data = (char *)shmat(Buffer->SegmentInfo.shmid,
																		  0, 0);
		Buffer->SegmentInfo.readOnly = False;
		
		XShmAttach(PlatformState->XDisplay,
				   &Buffer->SegmentInfo);

		Buffer->Pixels = LinuxAllocateMemory(NewWidth * NewHeight * BytesPerPixel);
		LinuxLog(PlatformState, "Surface buffer (%dx%d) created.", NewWidth, NewHeight);
	}
	
	Buffer->Width = NewWidth;
	Buffer->Height = NewHeight;
	Buffer->BytesPerPixel = BytesPerPixel;
	Buffer->Pitch = BytesPerPixel * NewWidth;
}

static void
LinuxDisplaySurfaceBuffer(platform_state *PlatformState,
						  linux_surface_buffer *Buffer,
						  Window TargetWindow, GC TargetWindowGC)
{
	Assert(PlatformState);
	Assert(Buffer);

	if (!Buffer->Pixels)
		return;

	memcpy(Buffer->Image->data, Buffer->Pixels, Buffer->Width * Buffer->Height * Buffer->BytesPerPixel);
	XShmPutImage(PlatformState->XDisplay,
				 TargetWindow,
				 TargetWindowGC,
				 Buffer->Image,
				 0, 0, 0, 0,
				 Buffer->Width, Buffer->Height,
				 False);
}

int
main(int NumParams, char **Params)
{
	platform_state PlatformState = {};
	PlatformState.Running = true;
	
	PlatformState.NumParams = NumParams;
	PlatformState.Params = Params;

	// NOTE(ivan): Check if already running.
	// TODO(ivan): Implement the check!

	// NOTE(ivan): Obtain executable's file name and path.
	char ModuleName[2048] = {};
	Verify(readlink("/proc/self/exe", ModuleName, CountOf(ModuleName) - 1) != -1);

	char *PastLastSlash = ModuleName, *Ptr = ModuleName;
	while (*Ptr) {
		if (*Ptr == '/') // NOTE(ivan): readlink("/proc/self/exe") always returns path with NIX path separators.
			PastLastSlash = Ptr + 1;
		Ptr++;
	}
	strncpy(PlatformState.ExeName, PastLastSlash, CountOf(PlatformState.ExeName) - 1);
	strncpy(PlatformState.ExePath, ModuleName, PastLastSlash - ModuleName);

	strcpy(PlatformState.ExeNameNoExt, PlatformState.ExeName);
	char *LastDot = 0;
	for (Ptr = PlatformState.ExeNameNoExt; *Ptr; Ptr++) {
		if (*Ptr == '.')
			LastDot = Ptr;
	}
	if (LastDot)
		*LastDot = 0;

	// NOTE(ivan): Change current directory if requested.
	const char *WorkDir = LinuxCheckParamValue(&PlatformState, "-cwd");
	if (WorkDir)
		chdir(WorkDir);

	// NOTE(ivan): Initialize log file.
	// TODO(ivan): Implement the log!

	// NOTE(ivan): Initialize work queues for multithreading.
	LinuxInitializeWorkQueue(&PlatformState, &PlatformState.HighPriorityWorkQueue, 6);
	LinuxInitializeWorkQueue(&PlatformState, &PlatformState.LowPriorityWorkQueue, 2);

	// NOTE(ivan): Initialize platform API structure.
	platform_api PlatformAPI = {};
	PlatformAPI.CheckParamValue = LinuxCheckParamValue;
	PlatformAPI.CheckParam = LinuxCheckParam;
	PlatformAPI.Log = LinuxLog;
	PlatformAPI.Error = LinuxError;
	PlatformAPI.AllocateMemory = LinuxAllocateMemory;
	PlatformAPI.DeallocateMemory = LinuxDeallocateMemory;
	PlatformAPI.GetMemoryStats = LinuxGetMemoryStats;
	PlatformAPI.AddWorkQueueEntry = LinuxAddWorkQueueEntry;
	PlatformAPI.CompleteWorkQueue = LinuxCompleteWorkQueue;
	PlatformAPI.ReadEntireFile = LinuxReadEntireFile;
	PlatformAPI.FreeEntireFileMemory = LinuxFreeEntireFileMemory;
	PlatformAPI.WriteEntireFile = LinuxWriteEntireFile;
	PlatformAPI.ReloadEntitiesModule = LinuxReloadEntitiesModule;
	PlatformAPI.ShouldEntitiesModuleBeReloaded = LinuxShouldEntitiesModuleBeReloaded;

	PlatformAPI.HighPriorityWorkQueue = &PlatformState.HighPriorityWorkQueue;
	PlatformAPI.LowPriorityWorkQueue = &PlatformState.LowPriorityWorkQueue;

	PlatformAPI.ExePath = PlatformState.ExePath;
	PlatformAPI.ExeName = PlatformState.ExeName;
	PlatformAPI.ExeNameNoExt = PlatformState.ExeNameNoExt;

	// NOTE(ivan): Initialize input structure for future use.
	game_input Input = {};
	
	// NOTE(ivan): Connect to X server.
	PlatformState.XDisplay = XOpenDisplay(getenv("DISPLAY"));
	if (!PlatformState.XDisplay) {
		if (getenv("DISPLAY"))
			LinuxError(&PlatformState, "Cannot connect to X display [:%s]!", getenv("DISPLAY"));
		else
			LinuxError(&PlatformState, "Cannot connect to X display!");
	}
	PlatformState.XScreen = DefaultScreen(PlatformState.XDisplay);
	PlatformState.XDepth = DefaultDepth(PlatformState.XDisplay,
										PlatformState.XScreen);
	PlatformState.XRootWindow = RootWindow(PlatformState.XDisplay,
										   PlatformState.XScreen);
	PlatformState.XVisual = DefaultVisual(PlatformState.XDisplay,
										  PlatformState.XScreen);
	PlatformState.XBlack = BlackPixel(PlatformState.XDisplay,
									  PlatformState.XScreen);
	PlatformState.XWhite = WhitePixel(PlatformState.XDisplay,
									  PlatformState.XScreen);

	// NOTE(ivan): Check X11 MIT-SHM support.
	s32 ShmMajor, ShmMinor;
	Bool ShmPixmaps;
	if (XShmQueryVersion(PlatformState.XDisplay,
						 &ShmMajor, &ShmMinor,
						 &ShmPixmaps))
		LinuxLog(&PlatformState, "X11 MIT-SHM extension version %d.%d.", ShmMajor, ShmMinor);
	else
		LinuxError(&PlatformState, "X11 MIT-SHM extension missing!");

	// NOTE(ivan): Create main window and its GC.
	XSetWindowAttributes WindowAttr;
	WindowAttr.background_pixel = PlatformState.XBlack;
	WindowAttr.border_pixel = PlatformState.XBlack;
	WindowAttr.event_mask = StructureNotifyMask | ExposureMask | KeyPressMask | ButtonPressMask;

	PlatformState.MainWindow = XCreateWindow(PlatformState.XDisplay,
											 PlatformState.XRootWindow,
											 DEF_WINDOW_X, DEF_WINDOW_Y,
											 DEF_WINDOW_WIDTH, DEF_WINDOW_HEIGHT,
											 5,
											 PlatformState.XDepth,
											 InputOutput,
											 PlatformState.XVisual,
											 CWBackPixel | CWBorderPixel | CWEventMask,
											 &WindowAttr);

	XSizeHints SizeHint;
	SizeHint.x = DEF_WINDOW_X;
	SizeHint.y = DEF_WINDOW_Y;
	SizeHint.width = DEF_WINDOW_WIDTH;
	SizeHint.height = DEF_WINDOW_HEIGHT;
	SizeHint.flags = PPosition | PSize;
	XSetStandardProperties(PlatformState.XDisplay,
						   PlatformState.MainWindow,
						   GAMENAME,
						   0, None,
						   Params, NumParams,
						   &SizeHint);

	XWMHints WMHint;
	WMHint.input = True;
	WMHint.flags = InputHint;
	XSetWMHints(PlatformState.XDisplay,
				PlatformState.MainWindow,
				&WMHint);

	PlatformState.WMDeleteWindow = XInternAtom(PlatformState.XDisplay,
											   "WM_DELETE_WINDOW",
											   False);
	XSetWMProtocols(PlatformState.XDisplay,
					PlatformState.MainWindow,
					&PlatformState.WMDeleteWindow,
					1);

	PlatformState.MainWindowGC = XCreateGC(PlatformState.XDisplay,
										   PlatformState.MainWindow,
										   0, 0);
	XSetBackground(PlatformState.XDisplay,
				   PlatformState.MainWindowGC,
				   PlatformState.XBlack);
	XSetForeground(PlatformState.XDisplay,
				   PlatformState.MainWindowGC,
				   PlatformState.XWhite);

	// NOTE(ivan): Force surface buffer generation to avoid 'Segmentation fault'.
	linux_window_client_dimension WindowDim = LinuxGetWindowClientDimension(PlatformState.XDisplay,
																			PlatformState.MainWindow);
	LinuxResizeSurfaceBuffer(&PlatformState,
							 &PlatformState.SurfaceBuffer,
							 WindowDim.Width,
							 WindowDim.Height);

	// NOTE(ivan): Initialize game state structure.
	game_state State = {};
	State.Type = GameStateType_Prepare;
	
	// NOTE(ivan): Present main window after all initialization is done.
	XMapRaised(PlatformState.XDisplay,
			   PlatformState.MainWindow);
	XFlush(PlatformState.XDisplay);

	// NOTE(ivan): Prepare timings.
	game_clocks Clocks = {};

	struct timespec LastCycleCounter = LinuxGetClock();
	struct timespec LastFPSCounter = LinuxGetClock();
	u32 NumFrames = 0;

	// NOTE(ivan): Primary cycle;
	while (PlatformState.Running) {
		// NOTE(ivan): Reset keyboard half-transition counters.
		for (u32 ButtonIndex = 0; ButtonIndex < CountOf(Input.KeyboardButtons); ButtonIndex++)
			Input.KeyboardButtons[ButtonIndex].HalfTransitionCount = 0;
		
		// NOTE(ivan): Process X11 messages.
		XEvent Event;
		while (XPending(PlatformState.XDisplay)) {
			XNextEvent(PlatformState.XDisplay,
					   &Event);
			if (Event.xany.window == PlatformState.MainWindow) {
				switch(Event.type) {
				case ClientMessage: {
					if (Event.xclient.data.l[0] == PlatformState.WMDeleteWindow)
						PlatformState.Running = false;
				} break;
					
				case ConfigureNotify: { // TODO(ivan): Don't regenerate surface buffer when the window moves.
					linux_window_client_dimension WindowDim = LinuxGetWindowClientDimension(PlatformState.XDisplay,
																							PlatformState.MainWindow);
					LinuxResizeSurfaceBuffer(&PlatformState,
											 &PlatformState.SurfaceBuffer,
											 WindowDim.Width,
											 WindowDim.Height);
				} break;

				case KeyPress: {
				} break;

				case ButtonPress: {
				} break;
				}
			}
		}

		// NOTE(ivan): Prepare offscreen graphics buffer.
		game_surface_buffer SurfaceBuffer;
		SurfaceBuffer.Pixels = PlatformState.SurfaceBuffer.Pixels;
		SurfaceBuffer.Width = PlatformState.SurfaceBuffer.Width;
		SurfaceBuffer.Height = PlatformState.SurfaceBuffer.Height;
		SurfaceBuffer.BytesPerPixel = PlatformState.SurfaceBuffer.BytesPerPixel;
		SurfaceBuffer.Pitch = PlatformState.SurfaceBuffer.Pitch;

		// NOTE(ivan): Game update.
		UpdateGame(&PlatformState,
				   &PlatformAPI,
				   &Clocks,
				   &SurfaceBuffer,
				   &Input,
				   &State);

		// NOTE(ivan): Display offscreen graphics buffer.
		LinuxDisplaySurfaceBuffer(&PlatformState,
								  &PlatformState.SurfaceBuffer,
								  PlatformState.MainWindow,
								  PlatformState.MainWindowGC);

		// NOTE(ivan): Make keyboard input data obsolete for next frame.
		for (u32 ButtonIndex = 0; ButtonIndex < CountOf(Input.KeyboardButtons); ButtonIndex++)
			Input.KeyboardButtons[ButtonIndex].IsActual = false;

		// NOTE(ivan): Quit if requested.
		if (PlatformAPI.QuitRequested)
			PlatformState.Running = false;

		// NOTE(ivan): Finish timings.
		struct timespec EndCycleCounter = LinuxGetClock();
		struct timespec EndFPSCounter = LinuxGetClock();

		Clocks.SecondsPerFrame = LinuxGetSecondsElapsed(LastCycleCounter, EndCycleCounter);
		LastCycleCounter = EndCycleCounter;

		f32 SecondsElapsed = LinuxGetSecondsElapsed(LastFPSCounter, EndFPSCounter);
		if (SecondsElapsed >= 1.0) {
			LastFPSCounter = EndFPSCounter;
			Clocks.FramesPerSecond = NumFrames;
			NumFrames = 0;
		} else {
			NumFrames++;
		}

		// NOTE(ivan): Now next frame won't be the first one again!
		State.Type = GameStateType_Frame;
	};

	// NOTE(ivan): Game uninitialization.
	State.Type = GameStateType_Release;
	UpdateGame(&PlatformState,
			   &PlatformAPI,
			   0,
			   0,
			   0,
			   &State);

	// NOTE(ivan): Destroy surface buffer.
	LinuxResizeSurfaceBuffer(&PlatformState,
							 &PlatformState.SurfaceBuffer,
							 0, 0);

	// NOTE(ivan): Destroy main window and its GC.
	XFreeGC(PlatformState.XDisplay,
			PlatformState.MainWindowGC);
	XDestroyWindow(PlatformState.XDisplay,
				   PlatformState.MainWindow);

	// NOTE(ivan): Disconnect from X server.
	XCloseDisplay(PlatformState.XDisplay);
	
	return 0;
}
