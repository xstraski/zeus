#include "game.h"
#include "game_platform.h"
#include "game_platform_linux.h"

#include "ents.h"

// NOTE(ivan): Default values of main window geometry.
#define DEF_WINDOW_X 20
#define DEF_WINDOW_Y 20
#define DEF_WINDOW_WIDTH 1024
#define DEF_WINDOW_HEIGHT 768

// NOTE(ivan): For fullscreen toggling.
#define _NET_WM_STATE_REMOVE 0
#define _NET_WM_STATE_ADD 1
#define _NET_WM_STATE_TOGGLE 2

// NOTE(ivan): Joysticks definitions.
#define XBOX_CONTROLLER_DEADZONE 5000

#define XBOX_CONTROLLER_AXIS_LEFT_THUMB_X 0
#define XBOX_CONTROLLER_AXIS_LEFT_THUMB_Y 1
#define XBOX_CONTROLLER_AXIS_RIGHT_THUMB_X 2
#define XBOX_CONTROLLER_AXIS_RIGHT_THUMB_Y 3
#define XBOX_CONTROLLER_AXIS_RIGHT_TRIGGER 4
#define XBOX_CONTROLLER_AXIS_LEFT_TRIGGER 5
#define XBOX_CONTROLLER_AXIS_DPAD_HORZ 6
#define XBOX_CONTROLLER_AXIS_DPAD_VERT 7

#define XBOX_CONTROLLER_BUTTON_A 0
#define XBOX_CONTROLLER_BUTTON_B 1
#define XBOX_CONTROLLER_BUTTON_X 2
#define XBOX_CONTROLLER_BUTTON_Y 3
#define XBOX_CONTROLLER_BUTTON_LEFT_SHOULDER 4
#define XBOX_CONTROLLER_BUTTON_RIGHT_SHOULDER 5
#define XBOX_CONTROLLER_BUTTON_BACK 6
#define XBOX_CONTROLLER_BUTTON_START 7
#define XBOX_CONTROLLER_BUTTON_LEFT_THUMB 9
#define XBOX_CONTROLLER_BUTTON_RIGHT_THUMB 10

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

	if (PlatformState->LogFile != -1)
		write(PlatformState->LogFile, FinalMessage, strlen(FinalMessage));
	
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

	b32 Result = false;
	
	int File = open(FileName, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
	if (File != -1) {
		ssize_t BytesWritten = write(File, Memory, Bytes);
		if (fsync(File) >= 0)
			Result = (BytesWritten == (ssize_t)Bytes);
		
		close(File);
	}
	
	return Result;
}

static b32
LinuxDirectoryExists(const char *DirName)
{
	Assert(DirName);

	DIR *Dir = opendir(DirName);
	if (!Dir)
		return false;

	closedir(Dir);
	return true;
}

static b32
LinuxCopyFile(const char *FileName, const char *NewName)
{
	Assert(FileName);
	Assert(NewName);

	b32 Result = false;
	
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
							Result = true;
						}
						
						close(ToFile);
					}
				}
				
				LinuxDeallocateMemory(Buffer);
			}
		}

		close(FromFile);
	}

	return Result;
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
	
	if (!LinuxCopyFile(FileName, TempFileName))
		LinuxError(PlatformState, "Failed copying entities module!");
	
	PlatformState->EntitiesLibrary = dlopen(TempFileName, RTLD_NOW | RTLD_LOCAL);
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
LinuxFindJoysticks(platform_state *PlatformState)
{
	Assert(PlatformState);

	char JoyFileName[] = "/dev/input/jsX";

	char Buffer[1024] = {};

	if (PlatformState->JoyEffect.id != -1) {
		PlatformState->JoyEffect.type = FF_RUMBLE;
		PlatformState->JoyEffect.u.rumble.strong_magnitude = 60000;
		PlatformState->JoyEffect.u.rumble.weak_magnitude = 0;
		PlatformState->JoyEffect.replay.length = 200;
		PlatformState->JoyEffect.replay.delay = 0;
		PlatformState->JoyEffect.id = -1; // NOTE(ivan): ID must be set -1 for every new effect.
	}

	for (u8 Index = 0; Index < CountOf(PlatformState->JoystickIDs); Index++) {
		u8 JoystickID = PlatformState->JoystickIDs[PlatformState->NumJoysticks];
		if (PlatformState->JoystickFDs[JoystickID] && PlatformState->JoystickEDs[JoystickID]) {
			PlatformState->NumJoysticks++;
			continue;
		} else if (!PlatformState->JoystickFDs[JoystickID]) {
			JoyFileName[13] = (char)(Index + 0x30);
			int JoyFile = open(JoyFileName, O_RDONLY | O_NONBLOCK);
			if (JoyFile == -1)
				continue;

			PlatformState->JoystickFDs[JoystickID] = JoyFile;
		}

		if (!PlatformState->JoystickEDs[JoystickID]) {
			// NOTE(ivan): Match the joystick file IDs with the event IDs.
			for (u8 TempIndex = 0; TempIndex <= 99; TempIndex++) {
				snprintf(Buffer, CountOf(Buffer) - 1, "/sys/class/input/js%d/device/event%d", Index, TempIndex);
				if (LinuxDirectoryExists(Buffer)) {
					snprintf(Buffer, CountOf(Buffer) - 1, "/dev/input/event%d", TempIndex);
					int EventFile = open(Buffer, O_RDWR);
					if (EventFile != -1) {
						PlatformState->JoystickEDs[Index] = EventFile;

						// NOTE(ivan): Send the effect to the driver.
						if (ioctl(EventFile, EVIOCSFF, &PlatformState->JoyEffect) == -1) {
							// TODO(ivan): Error.
						}

						break;
					}
				}
			}

			PlatformState->JoystickIDs[PlatformState->NumJoysticks++] = Index;
		} else {
			PlatformState->NumJoysticks++;
		}
	}
}

static void
LinuxReleaseJoysticks(platform_state *PlatformState)
{
	Assert(PlatformState);

	// TODO(ivan): Remove unplugged joysticks?
	for (u8 Index = 0; Index < MAX_JOYSTICKS; Index++) {
		if (PlatformState->JoystickIDs[Index]) {
			if (PlatformState->JoystickEDs[PlatformState->JoystickIDs[Index]] > 0)
				close(PlatformState->JoystickEDs[PlatformState->JoystickIDs[Index]]);

			if (PlatformState->JoystickFDs[PlatformState->JoystickIDs[Index]] > 0)
				close(PlatformState->JoystickFDs[PlatformState->JoystickIDs[Index]]);
		}
	}
}

inline void
LinuxProcessKeyboardOrMouseButton(game_input_button *Button,
								  b32 IsDown)
{
	Assert(Button);

	if (Button->WasDown != IsDown)
		Button->HalfTransitionCount++;
	Button->WasDown = Button->IsDown;
	Button->IsDown = IsDown;
	Button->IsActual = true;
}

inline void
LinuxProcessXboxDigitalButton(u32 ButtonState,
							  game_input_button *Button)
{
	Assert(Button);

	b32 IsDown = (ButtonState != 0);
	if (Button->WasDown != IsDown)
		Button->HalfTransitionCount = 1;
	else
		Button->HalfTransitionCount = 0;
	Button->WasDown = Button->IsDown;
	Button->IsDown = IsDown;
	Button->IsActual = true;
}

inline f32
LinuxProcessXboxAnalogStick(s16 Value,
							u16 DeadZoneThreshold)
{
	f32 Result = 0.0f;

	if (Value < -DeadZoneThreshold)
		Result = (f32)((Value + DeadZoneThreshold) / (32768.0f - DeadZoneThreshold));
	else if (Value > DeadZoneThreshold)
		Result = (f32)((Value - DeadZoneThreshold) / (32767.0f - DeadZoneThreshold));

	return Result;
}

static b32
LinuxMapXKeySymToKeyCode(KeySym XKeySym, key_code *OutCode)
{
	Assert(OutCode);

	key_code KeyCode;
	b32 KeyFound = false;
#define KeyMap(MapXKeyCode, MapKeyCode)				\
	if (XKeySym == MapXKeyCode) {					\
		KeyCode = MapKeyCode;						\
		KeyFound = true;							\
	}
	KeyMap(XK_Return, KeyCode_Enter);
	KeyMap(XK_Tab, KeyCode_Tab);
	KeyMap(XK_Escape, KeyCode_Escape);
	KeyMap(XK_KP_Space, KeyCode_Space);
	KeyMap(XK_BackSpace, KeyCode_BackSpace);
	KeyMap(XK_Shift_L, KeyCode_LeftShift);
	KeyMap(XK_Shift_R, KeyCode_RightShift);
	KeyMap(XK_Alt_L, KeyCode_LeftAlt);
	KeyMap(XK_Alt_R, KeyCode_RightAlt);
	KeyMap(XK_Control_L, KeyCode_LeftControl);
	KeyMap(XK_Control_R, KeyCode_RightControl);
	KeyMap(XK_Super_L, KeyCode_LeftSuper);
	KeyMap(XK_Super_R, KeyCode_RightSuper);
  	KeyMap(XK_Home, KeyCode_Home);
	KeyMap(XK_End, KeyCode_End);
	KeyMap(XK_Prior, KeyCode_PageUp);
	KeyMap(XK_Next, KeyCode_PageDown);
	KeyMap(XK_Insert, KeyCode_Insert);
	KeyMap(XK_Delete, KeyCode_Delete);
	KeyMap(XK_Up, KeyCode_Up);
	KeyMap(XK_Down, KeyCode_Down);
	KeyMap(XK_Left, KeyCode_Left);
	KeyMap(XK_Right, KeyCode_Right);
	KeyMap(XK_F1, KeyCode_F1);
	KeyMap(XK_F2, KeyCode_F2);
	KeyMap(XK_F3, KeyCode_F3);
	KeyMap(XK_F4, KeyCode_F4);
	KeyMap(XK_F5, KeyCode_F5);
	KeyMap(XK_F6, KeyCode_F6);
	KeyMap(XK_F7, KeyCode_F7);
	KeyMap(XK_F8, KeyCode_F8);
	KeyMap(XK_F9, KeyCode_F9);
	KeyMap(XK_F10, KeyCode_F10);
	KeyMap(XK_F11, KeyCode_F11);
	KeyMap(XK_F12, KeyCode_F12);
	
	KeyMap(XK_Num_Lock, KeyCode_NumLock);
	KeyMap(XK_Caps_Lock, KeyCode_CapsLock);
	KeyMap(XK_Scroll_Lock, KeyCode_ScrollLock);
	
	KeyMap(XK_Print, KeyCode_PrintScreen);
	KeyMap(XK_Pause, KeyCode_Pause);

	KeyMap(XK_a, KeyCode_A);
	KeyMap(XK_b, KeyCode_B);
	KeyMap(XK_c, KeyCode_C);
	KeyMap(XK_d, KeyCode_D);
	KeyMap(XK_e, KeyCode_E);
	KeyMap(XK_f, KeyCode_F);
	KeyMap(XK_g, KeyCode_G);
	KeyMap(XK_h, KeyCode_H);
	KeyMap(XK_i, KeyCode_I);
	KeyMap(XK_j, KeyCode_J);
	KeyMap(XK_k, KeyCode_K);
	KeyMap(XK_l, KeyCode_L);
	KeyMap(XK_m, KeyCode_M);
	KeyMap(XK_n, KeyCode_N);
	KeyMap(XK_o, KeyCode_O);
	KeyMap(XK_p, KeyCode_P);
	KeyMap(XK_q, KeyCode_Q);
	KeyMap(XK_r, KeyCode_R);
	KeyMap(XK_s, KeyCode_S);
	KeyMap(XK_t, KeyCode_T);
	KeyMap(XK_u, KeyCode_U);
	KeyMap(XK_v, KeyCode_V);
	KeyMap(XK_w, KeyCode_W);
	KeyMap(XK_x, KeyCode_X);
	KeyMap(XK_y, KeyCode_Y);
	KeyMap(XK_z, KeyCode_Z);

	KeyMap(XK_0, KeyCode_0);
	KeyMap(XK_1, KeyCode_1);
	KeyMap(XK_2, KeyCode_2);
	KeyMap(XK_3, KeyCode_3);
	KeyMap(XK_4, KeyCode_4);
	KeyMap(XK_5, KeyCode_5);
	KeyMap(XK_6, KeyCode_6);
	KeyMap(XK_7, KeyCode_7);
	KeyMap(XK_8, KeyCode_8);
	KeyMap(XK_9, KeyCode_9);

	KeyMap(XK_bracketleft, KeyCode_OpenBracket);
	KeyMap(XK_bracketright, KeyCode_CloseBracket);
	KeyMap(XK_semicolon, KeyCode_Semicolon);
	KeyMap(XK_quotedbl, KeyCode_Quote);
	KeyMap(XK_comma, KeyCode_Comma);
	KeyMap(XK_period, KeyCode_Period);
	KeyMap(XK_slash, KeyCode_Slash);
	KeyMap(XK_backslash, KeyCode_BackSlash);
	KeyMap(XK_asciitilde, KeyCode_Tilde);
	KeyMap(XK_plus, KeyCode_Plus);
	KeyMap(XK_minus, KeyCode_Minus);

	KeyMap(XK_KP_Up, KeyCode_NumUp);
	KeyMap(XK_KP_Down, KeyCode_NumDown);
	KeyMap(XK_KP_Left, KeyCode_NumLeft);
	KeyMap(XK_KP_Right, KeyCode_NumRight);
	KeyMap(XK_KP_Home, KeyCode_NumHome);
	KeyMap(XK_KP_End, KeyCode_NumEnd);
	KeyMap(XK_KP_Prior, KeyCode_NumPageUp);
	KeyMap(XK_KP_Next, KeyCode_NumPageDown);
	KeyMap(XK_KP_Multiply, KeyCode_NumMultiply);
	KeyMap(XK_KP_Divide, KeyCode_NumDivide);
	KeyMap(XK_KP_Add, KeyCode_NumPlus);
	KeyMap(XK_KP_Subtract, KeyCode_NumMinus);
	KeyMap(XK_KP_Separator, KeyCode_NumClear);
#undef KeyMap	

	if (!KeyFound)
		return false;
	*OutCode = KeyCode;
	return true;
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

// TODO(ivan): This is temporary, we should support all possible video modes.
static void
LinuxToggleFullscreen(platform_state *PlatformState, Window Wnd)
{
	Assert(PlatformState);
	Assert(Wnd);

	static b32 IsFullscreen = false;
	IsFullscreen = !IsFullscreen;
	
	Atom Fullscreen = XInternAtom(PlatformState->XDisplay, "_NET_WM_STATE_FULLSCREEN", False);
	Atom WindowState = XInternAtom(PlatformState->XDisplay, "_NET_WM_STATE", False);
	s32 Mask = SubstructureNotifyMask | SubstructureRedirectMask;

	XEvent Event = {};
	Event.xclient.type = ClientMessage;
	Event.xclient.send_event = True;
	Event.xclient.window = Wnd;
	Event.xclient.message_type = WindowState;
	Event.xclient.format = 32;
	Event.xclient.data.l[0] = IsFullscreen ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
	Event.xclient.data.l[1] = (long)Fullscreen;
	Event.xclient.data.l[2] = 0;

	XSendEvent(PlatformState->XDisplay,
			   PlatformState->XRootWindow,
			   False,
			   Mask,
			   &Event);
}

static Cursor
LinuxCreateNullCursor(Display *XDisplay, Window RootWindow)
{
	Assert(XDisplay);
	Assert(RootWindow);
	
	Pixmap CursorMask;
	XGCValues XGC;
	GC CursorGC;
	XColor DummyColor;
	Cursor Result;

	CursorMask = XCreatePixmap(XDisplay,
							   RootWindow,
							   1, 1, 1);
	XGC.function = GXclear;
	CursorGC = XCreateGC(XDisplay,
						 CursorMask,
						 GCFunction,
						 &XGC);
	XFillRectangle(XDisplay,
				   CursorMask,
				   CursorGC,
				   0, 0, 1, 1);

	DummyColor.pixel = 0;
	DummyColor.red = 0;
	DummyColor.flags = 0;
	Result = XCreatePixmapCursor(XDisplay,
								 CursorMask, CursorMask,
								 &DummyColor, &DummyColor,
								 0, 0);
	XFreePixmap(XDisplay, CursorMask);
	XFreeGC(XDisplay, CursorGC);

	return Result;
}

int
main(int NumParams, char **Params)
{
	platform_state PlatformState = {};
	PlatformState.Running = true;
	
	PlatformState.NumParams = NumParams;
	PlatformState.Params = Params;

#if INTERNAL
	PlatformState.DebugCursor = true;
#endif	
	
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
#if INTERNAL
	char LogName[1024] = {};
	snprintf(LogName, CountOf(LogName) - 1, "%s.log", PlatformState.ExeNameNoExt);

	PlatformState.LogFile = open(LogName, O_WRONLY | O_CREAT, S_IWUSR | S_IRUSR);
	if (PlatformState.LogFile != -1) {
		u8 UnicodeBOM[] = {0xEF, 0xBB, 0xBF}; // NOTE(ivan): UTF-8.
		write(PlatformState.LogFile, UnicodeBOM, sizeof(UnicodeBOM));
	}
#endif // #if INTERNAL	

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

	// NOTE(ivan): Initialize joysticks.
	LinuxFindJoysticks(&PlatformState);

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
		LinuxError(&PlatformState, "X11 MIT-SHM extension is unavailable!");

	// NOTE(ivan): Check X11 XKB support.
	s32 XkbMajor = XkbMajorVersion, XkbMinor = XkbMinorVersion;
	if (XkbLibraryVersion(&XkbMajor, &XkbMinor) == True)
		LinuxLog(&PlatformState, "X11 XKB extension version: compile-time %d.%d, run-time %d.%d.", XkbMajorVersion, XkbMinorVersion, XkbMajor, XkbMinor);
	else
		LinuxError(&PlatformState, "X11 XKB extension is unavailable!");

	// NOTE(ivan): Create main window and its GC.
	XSetWindowAttributes WindowAttr;
	WindowAttr.background_pixel = PlatformState.XBlack;
	WindowAttr.border_pixel = PlatformState.XBlack;
	WindowAttr.event_mask = StructureNotifyMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | ButtonMotionMask | PointerMotionMask;

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
					
				case ConfigureNotify: {
					if (Event.xconfigure.width != PlatformState.SurfaceBuffer.Width &&
						Event.xconfigure.height != PlatformState.SurfaceBuffer.Height) {
						linux_window_client_dimension WindowDim = LinuxGetWindowClientDimension(PlatformState.XDisplay,
																								PlatformState.MainWindow);
						LinuxResizeSurfaceBuffer(&PlatformState,
												 &PlatformState.SurfaceBuffer,
												 WindowDim.Width,
												 WindowDim.Height);
					}
				} break;

				case KeyPress:
				case KeyRelease: {
					KeySym XKeySym = XLookupKeysym(&Event.xkey, 0);
					
					key_code KeyCode;
					if (LinuxMapXKeySymToKeyCode(XKeySym, &KeyCode))
						LinuxProcessKeyboardOrMouseButton(&Input.KeyboardButtons[KeyCode], Event.type == KeyPress);
				} break;

				case ButtonPress:
				case ButtonRelease: {
					b32 IsKeyPress = (Event.type == ButtonPress);
					switch (Event.xbutton.button) {
					case Button1: {
						LinuxProcessKeyboardOrMouseButton(&Input.MouseButtons[0], IsKeyPress);
					} break;

					case Button2: {
						LinuxProcessKeyboardOrMouseButton(&Input.MouseButtons[1], IsKeyPress);
					} break;

					case Button3: {
						LinuxProcessKeyboardOrMouseButton(&Input.MouseButtons[2], IsKeyPress);
					} break;

					case Button4: {
						LinuxProcessKeyboardOrMouseButton(&Input.MouseButtons[3], IsKeyPress);
					} break;

					case Button5: {
						LinuxProcessKeyboardOrMouseButton(&Input.MouseButtons[4], IsKeyPress);
					} break;
					}
				} break;
				}
			}
		}

		// NOTE(ivan): Capture mouse pointer position.
		Window RootIgnore, ChildIgnore;
		s32 RootXIgnore, RootYIgnore;
		s32 WinX, WinY;
		u32 MaskIgnore;
		XQueryPointer(PlatformState.XDisplay,
					  PlatformState.MainWindow,
					  &RootIgnore, &ChildIgnore,
					  &RootXIgnore, &RootYIgnore,
					  &WinX, &WinY,
					  &MaskIgnore);
		Input.MouseX = WinX;
		Input.MouseY = WinY;

		// NOTE(ivan): Process joysticks input.
		if (PlatformState.NumJoysticks) {
			u8 NumJoysticks = PlatformState.NumJoysticks;
			if (NumJoysticks > CountOf(Input.Controllers))
				NumJoysticks = CountOf(Input.Controllers);
			
			for (u8 Index = 0; Index < NumJoysticks; Index++) {
				u8 JoystickID = PlatformState.JoystickIDs[Index];
				game_input_controller *Joystick = &Input.Controllers[Index];

				Joystick->IsConnected = true;

				struct js_event JoyEvent;
				while (read(PlatformState.JoystickFDs[Index], &JoyEvent, sizeof(JoyEvent)) > 0) {
					if (JoyEvent.type >= JS_EVENT_INIT)
						JoyEvent.type -= JS_EVENT_INIT;

					if (JoyEvent.type == JS_EVENT_BUTTON) {
						if (JoyEvent.number == XBOX_CONTROLLER_BUTTON_A) {
							LinuxProcessXboxDigitalButton((u32)JoyEvent.value,
														  &Joystick->A);
						} else if (JoyEvent.number == XBOX_CONTROLLER_BUTTON_B) {
							LinuxProcessXboxDigitalButton((u32)JoyEvent.value,
														  &Joystick->B);
						} else if (JoyEvent.number == XBOX_CONTROLLER_BUTTON_X) {
							LinuxProcessXboxDigitalButton((u32)JoyEvent.value,
														  &Joystick->X);
						} else if (JoyEvent.number == XBOX_CONTROLLER_BUTTON_Y) {
							LinuxProcessXboxDigitalButton((u32)JoyEvent.value,
														  &Joystick->Y);
						} else if (JoyEvent.number == XBOX_CONTROLLER_BUTTON_START) {
							LinuxProcessXboxDigitalButton((u32)JoyEvent.value,
														  &Joystick->Start);
						} else if (JoyEvent.number == XBOX_CONTROLLER_BUTTON_BACK) {
							LinuxProcessXboxDigitalButton((u32)JoyEvent.value,
														  &Joystick->Back);
						} else if (JoyEvent.number == XBOX_CONTROLLER_BUTTON_LEFT_SHOULDER) {
							LinuxProcessXboxDigitalButton((u32)JoyEvent.value,
														  &Joystick->LeftBumper);
						} else if (JoyEvent.number == XBOX_CONTROLLER_BUTTON_RIGHT_SHOULDER) {
							LinuxProcessXboxDigitalButton((u32)JoyEvent.value,
														  &Joystick->RightBumper);
						} else if (JoyEvent.number == XBOX_CONTROLLER_BUTTON_LEFT_THUMB) {
							LinuxProcessXboxDigitalButton((u32)JoyEvent.value,
														  &Joystick->LeftStick);
						} else if (JoyEvent.number == XBOX_CONTROLLER_BUTTON_RIGHT_THUMB) {
							LinuxProcessXboxDigitalButton((u32)JoyEvent.value,
														  &Joystick->RightStick);
						}
					} else if (JoyEvent.type == JS_EVENT_AXIS) {
						if (JoyEvent.number == XBOX_CONTROLLER_AXIS_LEFT_THUMB_X) {
							Joystick->LeftStickX = LinuxProcessXboxAnalogStick(JoyEvent.value, XBOX_CONTROLLER_DEADZONE);
						} else if (JoyEvent.number == XBOX_CONTROLLER_AXIS_LEFT_THUMB_Y) {
							Joystick->LeftStickY = LinuxProcessXboxAnalogStick(JoyEvent.value, XBOX_CONTROLLER_DEADZONE);
						} else if (JoyEvent.number == XBOX_CONTROLLER_AXIS_RIGHT_THUMB_X) {
							Joystick->RightStickX = LinuxProcessXboxAnalogStick(JoyEvent.value, XBOX_CONTROLLER_DEADZONE);
						} else if (JoyEvent.number == XBOX_CONTROLLER_AXIS_RIGHT_THUMB_Y) {
							Joystick->RightStickY = LinuxProcessXboxAnalogStick(JoyEvent.value, XBOX_CONTROLLER_DEADZONE);
						} else if (JoyEvent.number == XBOX_CONTROLLER_AXIS_DPAD_HORZ) {
							if (JoyEvent.value = -32767) {
								LinuxProcessXboxDigitalButton(1, &Joystick->Left);
								LinuxProcessXboxDigitalButton(0, &Joystick->Right);
							} else if (JoyEvent.value == 32768) {
								LinuxProcessXboxDigitalButton(0, &Joystick->Left);
								LinuxProcessXboxDigitalButton(1, &Joystick->Right);
							} else {
								LinuxProcessXboxDigitalButton(0, &Joystick->Left);
								LinuxProcessXboxDigitalButton(0, &Joystick->Right);
							}
						} else if (JoyEvent.number == XBOX_CONTROLLER_AXIS_DPAD_VERT) {
							if (JoyEvent.value = -32767) {
								LinuxProcessXboxDigitalButton(1, &Joystick->Up);
								LinuxProcessXboxDigitalButton(0, &Joystick->Down);
							} else if (JoyEvent.value == 32768) {
								LinuxProcessXboxDigitalButton(0, &Joystick->Up);
								LinuxProcessXboxDigitalButton(1, &Joystick->Down);
							} else {
								LinuxProcessXboxDigitalButton(0, &Joystick->Up);
								LinuxProcessXboxDigitalButton(0, &Joystick->Down);
							}
						}
					}
				}
			}
		}

		// NOTE(ivan): Process linux-side input events.
		if (Input.KeyboardButtons[KeyCode_LeftAlt].IsDown && Input.KeyboardButtons[KeyCode_F4].IsDown)
			PlatformState.Running = false;
#if INTERNAL
		if (IsSinglePress(Input.KeyboardButtons[KeyCode_F2]))
			PlatformState.DebugCursor = !PlatformState.DebugCursor;
#endif
		if (Input.KeyboardButtons[KeyCode_LeftAlt].IsDown && IsSinglePress(Input.KeyboardButtons[KeyCode_Enter]))
			LinuxToggleFullscreen(&PlatformState, PlatformState.MainWindow);

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

		// NOTE(ivan): Set cursor.
		if (PlatformState.DebugCursor)
			XUndefineCursor(PlatformState.XDisplay,
							PlatformState.MainWindow);
		else
			XDefineCursor(PlatformState.XDisplay,
						  PlatformState.MainWindow,
						  LinuxCreateNullCursor(PlatformState.XDisplay,
												PlatformState.MainWindow));
		
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

	// NOTE(ivan): Destroy joysticks.
	LinuxReleaseJoysticks(&PlatformState);

	// NOTE(ivan): Release work queues.
	LinuxReleaseWorkQueue(&PlatformState.HighPriorityWorkQueue);
	LinuxReleaseWorkQueue(&PlatformState.LowPriorityWorkQueue);

	// NOTE(ivan): Release log file.
	if (PlatformState.LogFile != -1)
		close(PlatformState.LogFile);
	
	return 0;
}
