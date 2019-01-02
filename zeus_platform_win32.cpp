#include "zeus.h"
#include "zeus_platform.h"
#include "zeus_platform_win32.h"

#include <versionhelpers.h>
#include <objbase.h>

#include <process.h>

PLATFORM_CHECK_PARAM(Win32CheckParam)
{
	Assert(PlatformState);
	Assert(Param);
	
	for (s32 Index = 0; Index < PlatformState->NumParams; Index++) {
		if (strcmp(PlatformState->Params[Index], Param) == 0)
			return Index;
	}

	return -1;
}

PLATFORM_CHECK_PARAM_VALUE(Win32CheckParamValue)
{
	Assert(PlatformState);
	Assert(Param);
	
	s32 Index = Win32CheckParam(PlatformState, Param);
	if (Index == -1)
		return 0;

	if ((Index + 1) >= PlatformState->NumParams)
		return 0;

	return PlatformState->Params[Index + 1];
}

PLATFORM_LOG(Win32Log)
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

	if (PlatformState->LogFile != INVALID_HANDLE_VALUE) {
		DWORD Unused;
		WriteFile(PlatformState->LogFile, FinalMessage, (DWORD)strlen(FinalMessage), &Unused, 0);
	}

	if (IsDebuggerPresent()) {
		OutputDebugStringA("## ");
		OutputDebugStringA(FinalMessage);
	}
	
	LeaveTicketMutex(&LogMutex);
#endif	
}

// TODO(ivan): Implement an appropriate and complete error reporter.
PLATFORM_ERROR(Win32Error)
{
	Assert(PlatformState);
	Assert(ErrorFormat);

#if INTERNAL	
	static ticket_mutex ErrorMutex = {};
	EnterTicketMutex(&ErrorMutex);
	
	static b32 InError = false;
	if (InError) {
		LeaveTicketMutex(&ErrorMutex);
		ExitProcess(0);
	}
	InError = true;
	
	char ErrorBuffer[2048];
	CollectArgsN(ErrorBuffer, CountOf(ErrorBuffer), ErrorFormat);

	Win32Log(PlatformState, "*** ERROR *** %s", ErrorBuffer);
	MessageBoxA(0, ErrorBuffer, GAMENAME " Error", MB_OK | MB_ICONERROR | MB_TOPMOST);

	LeaveTicketMutex(&ErrorMutex);
#endif	
	TerminateProcess(GetCurrentProcess(), 1);
}

// TODO(ivan): Implement a reliable Win32 memory allocator.
PLATFORM_ALLOCATE_MEMORY(Win32AllocateMemory)
{
	Assert(Bytes);
	return VirtualAlloc(0, Bytes, MEM_COMMIT, PAGE_READWRITE);
}

// TODO(ivan): Implement a reliable Win32 memory allocator.
PLATFORM_DEALLOCATE_MEMORY(Win32DeallocateMemory)
{
	if (!Address)
		return;
	VirtualFree(Address, 0, MEM_RELEASE);
}

PLATFORM_GET_MEMORY_STATS(Win32GetMemoryStats)
{
	platform_memory_stats Result = {};
	
	MEMORYSTATUSEX MemoryStatus;
	MemoryStatus.dwLength = sizeof(MemoryStatus);

	if (GlobalMemoryStatusEx(&MemoryStatus)) {
		Result.BytesTotal = MemoryStatus.ullTotalPhys;
		Result.BytesAvailable = MemoryStatus.ullAvailPhys;
	}

	return Result;
}

// NOTE(ivan): System structure for setting thread name by Win32SetThreadName().
#pragma pack(push, 8)
struct win32_thread_name_info {
	DWORD Type; // NOTE(ivan): Must be 0x1000.
	LPCSTR Name;
	DWORD ThreadId;
	DWORD Flags;
};
#pragma pack(pop)

inline void
Win32SetThreadName(DWORD ThreadId, const char *Name)
{
	Assert(Name);
	
	win32_thread_name_info NameInfo = {};
	NameInfo.Type = 0x1000;
	NameInfo.Name = Name;
	NameInfo.ThreadId = ThreadId;

#pragma warning(push)
#pragma warning(disable: 6320 6322)
	__try {
		RaiseException(0x406D1388, 0, sizeof(NameInfo) / sizeof(ULONG_PTR), (ULONG_PTR *)&NameInfo);
	} __except(EXCEPTION_EXECUTE_HANDLER) {}
#pragma warning(pop)
}

inline DWORD
Win32GetThreadId(void)
{
#if defined(_M_IX86)	
	u8 *ThreadLocalStorage = (u8 *)__readgsqword(0x18);
#elif defined(_M_X64) || defined(_M_AMD64)
	u8 *ThreadLocalStorage = (u8 *)__readgsqword(0x30);
#endif	
	return (DWORD)(*(u32 *)(ThreadLocalStorage + 0x48));
}

static b32
Win32DoNextWorkQueueEntry(work_queue *Queue)
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

static unsigned __stdcall
Win32WorkQueueProc(void *Param)
{
	work_queue_startup *Startup = (work_queue_startup *)Param;
	work_queue *Queue = Startup->Queue;

	u32 TestThreadId = Win32GetThreadId();
	Assert(TestThreadId == GetCurrentThreadId());

	while (true) {
		if (Win32DoNextWorkQueueEntry(Queue))
			WaitForSingleObjectEx(Queue->Semaphore, INFINITE, FALSE);
	}
}

static void
Win32InitializeWorkQueue(platform_state *PlatformState,
						 work_queue *Queue,
						 u32 ThreadCount)
{
	Assert(PlatformState);
	Assert(Queue);
	Assert(ThreadCount);

	Queue->CompletionGoal = Queue->CompletionCount = 0;
	Queue->NextEntryToWrite = Queue->NextEntryToRead = 0;

	u32 InitialCount = 0;
	Queue->Semaphore = CreateSemaphoreExA(0,
										  InitialCount,
										  ThreadCount,
										  0, 0,
										  SEMAPHORE_ALL_ACCESS);

	Queue->Startups = (work_queue_startup *)Win32AllocateMemory(sizeof(work_queue_startup) * ThreadCount);
	if (!Queue->Startups)
		Win32Error(PlatformState, "Failed allocating resources for work queue!");
	for (u32 ThreadIndex = 0; ThreadIndex < ThreadCount; ThreadIndex++) {
		Queue->Startups[ThreadIndex].Queue = Queue;

		DWORD ThreadId;
		HANDLE Thread = (HANDLE)_beginthreadex(0, 0, Win32WorkQueueProc, &Queue->Startups[ThreadIndex], 0, (unsigned int *)&ThreadId);
		CloseHandle(Thread);
	}
}

static void
Win32ReleaseWorkQueue(work_queue *Queue)
{
	Assert(Queue);
	Win32DeallocateMemory(Queue->Startups);
	CloseHandle(Queue->Semaphore);
}

PLATFORM_ADD_WORK_QUEUE_ENTRY(Win32AddWorkQueueEntry)
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
	ReleaseSemaphore(Queue->Semaphore, 1, 0);
}

PLATFORM_COMPLETE_WORK_QUEUE(Win32CompleteWorkQueue)
{
	while (Queue->CompletionGoal != Queue->CompletionCount)
		Win32DoNextWorkQueueEntry(Queue);

	Queue->CompletionGoal = Queue->CompletionCount = 0;
}

PLATFORM_READ_ENTIRE_FILE(Win32ReadEntireFile)
{
	Assert(FileName);

	piece Result = {};

	HANDLE File = CreateFileA(FileName, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, 0);
	if (File != INVALID_HANDLE_VALUE) {
		LARGE_INTEGER FileLargeSize;
		if (GetFileSizeEx(File, &FileLargeSize)) {
			u32 FileSize = SafeTruncateU64(FileLargeSize.QuadPart);
			Result.Bytes = FileSize;
			Result.Memory = (u8 *)Win32AllocateMemory(FileSize);
			if (Result.Memory) {
				DWORD Unused;
				if (ReadFile(File, Result.Memory, FileSize, &Unused, 0)) {
				} else {
					Win32DeallocateMemory(Result.Memory);
					Result.Memory = 0;
				}
			}
		}

		CloseHandle(File);
	}
	
	return Result;
}

PLATFORM_FREE_ENTIRE_FILE_MEMORY(Win32FreeEntireFileMemory)
{
	Assert(ReadResult);
	Assert(ReadResult->Memory);
	
	Win32DeallocateMemory(ReadResult->Memory);
}

PLATFORM_WRITE_ENTIRE_FILE(Win32WriteEntireFile)
{
	Assert(FileName);
	Assert(Memory);
	Assert(Bytes);

	b32 Result = true;
	
	HANDLE File = CreateFileA(FileName, GENERIC_WRITE, 0, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (File != INVALID_HANDLE_VALUE) {
		DWORD Unused;
		if (!WriteFile(File, Memory, Bytes, &Unused, 0))
			Result = false;
		
		CloseHandle(File);
	}
	return Result;
}

// NOTE(ivan): Dummy stubs for the case when XInput is not available.
static X_INPUT_GET_STATE(Win32XInputGetStateStub)
{
	UnreferencedParam(UserIndex);
	UnreferencedParam(State);
	
	return ERROR_DEVICE_NOT_CONNECTED;
}
static X_INPUT_SET_STATE(Win32XInputSetStateStub)
{
	UnreferencedParam(UserIndex);
	UnreferencedParam(Vibration);
	
	return ERROR_DEVICE_NOT_CONNECTED;
}

static x_input
Win32LoadXInput(platform_state *PlatformState)
{
	Assert(PlatformState);
	
	x_input Result;

	Result.Library = LoadLibraryA("xinput1_4.dll");
	if (!Result.Library)
		Result.Library = LoadLibraryA("xinput9_1_0.dll");
	if (!Result.Library)
		Result.Library = LoadLibraryA("xinput1_3.dll");
	if (!Result.Library) {
		Result.GetState = Win32XInputGetStateStub;
		Result.SetState = Win32XInputSetStateStub;
	} else {
		Win32Log(PlatformState, "Failed loading XInput library!");
		Result.GetState = (x_input_get_state *)GetProcAddress(Result.Library, "XInputGetState");
		Result.SetState = (x_input_set_state *)GetProcAddress(Result.Library, "XInputSetState");
	}

	return Result;
}

static void
Win32FreeXInput(x_input *XInput)
{
	Assert(XInput);
	FreeLibrary(XInput->Library);
}

inline void
Win32ProcessKeyboardOrMouseButton(game_input_button *Button,
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
Win32ProcessXInputDigitalButton(game_input_button *Button,
								DWORD XInputButtonState,
								DWORD ButtonBit)
{
	Assert(Button);

	b32 IsDown = ((XInputButtonState & ButtonBit) == ButtonBit);
	if (Button->WasDown != IsDown)
		Button->HalfTransitionCount = 1;
	else
		Button->HalfTransitionCount = 0;
	Button->WasDown = Button->IsDown;
	Button->IsDown = IsDown;
	Button->IsActual = true;
}

inline f32
Win32ProcessXInputStickValue(SHORT Value,
							 SHORT DeadZoneThreshold)
{
	f32 Result = 0.0f;

	if (Value < -DeadZoneThreshold)
		Result = (f32)((Value + DeadZoneThreshold) / (32768.0f - DeadZoneThreshold));
	else if (Value > DeadZoneThreshold)
		Result = (f32)((Value - DeadZoneThreshold) / (32767.0f - DeadZoneThreshold));

	return Result;
}

static b32
Win32MapVKToKeyCode(u32 VKCode,
					u32 ScanCode,
					b32 IsE0,
					b32 IsE1,
					key_code *OutCode) {
	Assert(OutCode);

	// TODO(ivan): Fully test this routine!

	key_code KeyCode = {}; // NOTE(ivan): Result of Windows VK -> our keycode conversion.
	b32 KeyFound = false;

	if (VKCode == 255) {
		// NOTE(ivan): Discard "fake keys" which are part of an escaped sequence.
		return false;
	} else if (VKCode == VK_SHIFT) {
		// NOTE(ivan): Correct left-hand / right-hand SHIFT
		VKCode = MapVirtualKey(ScanCode, MAPVK_VSC_TO_VK_EX);
	} else if (VKCode == VK_NUMLOCK) {
		// NOTE(ivan): Correct PAUSE/BREAK and NUMLOCK silliness, and set the extended bit.
		ScanCode = (MapVirtualKey(VKCode, MAPVK_VK_TO_VSC) | 0x100);
	}

	// NOTE(ivan): E0 and E1 are escape sequences used for certain special keys, such as PRINTSCREEN or PAUSE/BREAK.
	// See: http://www.win.tue.nl/~aeb/linux/kbd/scancodes-1.html
	
	if (IsE1) {
		// NOTE(ivan): For escaped sequences, turn the virtual key into the correct scan code using MapVirtualKey.
		// However, MapVirtualKey is unable to map VK_PAUSE (this is a known bug), hence we map that by hand.
		if (VKCode == VK_PAUSE)
			ScanCode = 0x45;
		else
			ScanCode = MapVirtualKey(VKCode, MAPVK_VK_TO_VSC);
	}

	switch (VKCode) {
		// NOTE(ivan): Right-hand CONTROL and ALT have their E0 bit set.
	case VK_CONTROL: {
		if (IsE0)
			KeyCode = KeyCode_RightControl;
		else
			KeyCode = KeyCode_LeftControl;
		KeyFound = true;
	} break;

	case VK_MENU: {
		if (IsE0)
			KeyCode = KeyCode_RightAlt;
		else
			KeyCode = KeyCode_LeftAlt;
		KeyFound = true;
	} break;

		// NOTE(ivan): NUM ENTER has its E0 bit set
	case VK_RETURN: {
		if (IsE0)
			KeyCode = KeyCode_NumEnter;
		else
			KeyCode = KeyCode_Enter;
		KeyFound = true;
	} break;

		// NOTE(ivan): The standard INSERT, DELETE, HOME, END, PRIOR and NEXT keys will always have their E0 bit set,
		// but the corresponding NUM keys will not.
	case VK_INSERT: {
		if (!IsE0)
			KeyCode = KeyCode_NumInsert;
		else
			KeyCode = KeyCode_Insert;
		KeyFound = true;
	} break;
	case VK_DELETE: {
		if (!IsE0)
			KeyCode = KeyCode_NumDelete;
		else
			KeyCode = KeyCode_Delete;
		KeyFound = true;
	} break;
	case VK_HOME: {
		if (!IsE0)
			KeyCode = KeyCode_NumHome;
		else
			KeyCode = KeyCode_Home;
		KeyFound = true;
	} break;
	case VK_END: {
		if (!IsE0)
			KeyCode = KeyCode_NumEnd;
		else
			KeyCode = KeyCode_End;
		KeyFound = true;
	} break;
	case VK_PRIOR: {
		if (!IsE0)
			KeyCode = KeyCode_NumPageUp;
		else
			KeyCode = KeyCode_PageUp;
		KeyFound = true;
	} break;
	case VK_NEXT: {
		if (!IsE0)
			KeyCode = KeyCode_NumPageDown;
		else
			KeyCode = KeyCode_PageDown;
		KeyFound = true;
	} break;

		// NOTE(ivan): The standard arrow keys will awlays have their E0 bit set,
		// but the corresponding NUM keys will not.
	case VK_UP: {
		if (!IsE0)
			KeyCode = KeyCode_NumUp;
		else
			KeyCode = KeyCode_Up;
		KeyFound = true;
	} break;
	case VK_DOWN: {
		if (!IsE0)
			KeyCode = KeyCode_NumDown;
		else
			KeyCode = KeyCode_Down;
		KeyFound = true;
	} break;
	case VK_LEFT: {
		if (!IsE0)
			KeyCode = KeyCode_NumLeft;
		else
			KeyCode = KeyCode_Left;
		KeyFound = true;
	} break;
	case VK_RIGHT: {
		if (!IsE0)
			KeyCode = KeyCode_NumRight;
		else
			KeyCode = KeyCode_Right;
		KeyFound = true;
	} break;

		// NOTE(ivan): NUM 5 doesn't have its E0 bit set.
	case VK_CLEAR: {
		if (!IsE0) {
			KeyCode = KeyCode_NumClear;
			KeyFound = true;
		} else {
			return false;
		}
	} break;
	}

#define KeyMap(MapVK, MapKeyCode)				\
	if (VKCode == MapVK) {						\
		KeyCode = MapKeyCode;					\
		KeyFound = true;						\
	}
	//KeyMap(VK_RETURN, KeyCode_Enter);
	KeyMap(VK_TAB, KeyCode_Tab);
	KeyMap(VK_ESCAPE, KeyCode_Escape);
	KeyMap(VK_SPACE, KeyCode_Space);
	KeyMap(VK_BACK, KeyCode_BackSpace);
	KeyMap(VK_LSHIFT, KeyCode_LeftShift);
	KeyMap(VK_RSHIFT, KeyCode_RightShift);
	KeyMap(VK_LMENU, KeyCode_LeftAlt);
	KeyMap(VK_RMENU, KeyCode_RightAlt);
	KeyMap(VK_LCONTROL, KeyCode_LeftControl);
	KeyMap(VK_RCONTROL, KeyCode_RightControl);
	KeyMap(VK_LWIN, KeyCode_LeftSuper);
	KeyMap(VK_RWIN, KeyCode_RightSuper);
  	//KeyMap(VK_HOME, KeyCode_Home);
	//KeyMap(VK_END, KeyCode_End);
	//KeyMap(VK_PRIOR, KeyCode_PageUp);
	//KeyMap(VK_NEXT, KeyCode_PageDown);
	//KeyMap(VK_INSERT, KeyCode_Insert);
	//KeyMap(VK_DELETE, KeyCode_Delete);
	//KeyMap(VK_UP, KeyCode_Up);
	//KeyMap(VK_DOWN, KeyCode_Down);
	//KeyMap(VK_LEFT, KeyCode_Left);
	//KeyMap(VK_RIGHT, KeyCode_Right);

	KeyMap(VK_F1, KeyCode_F1);
	KeyMap(VK_F2, KeyCode_F2);
	KeyMap(VK_F3, KeyCode_F3);
	KeyMap(VK_F4, KeyCode_F4);
	KeyMap(VK_F5, KeyCode_F5);
	KeyMap(VK_F6, KeyCode_F6);
	KeyMap(VK_F7, KeyCode_F7);
	KeyMap(VK_F8, KeyCode_F8);
	KeyMap(VK_F9, KeyCode_F9);
	KeyMap(VK_F10, KeyCode_F10);
	KeyMap(VK_F11, KeyCode_F11);
	KeyMap(VK_F12, KeyCode_F12);
	
	KeyMap(VK_NUMLOCK, KeyCode_NumLock);
	KeyMap(VK_CAPITAL, KeyCode_CapsLock);
	KeyMap(VK_SCROLL, KeyCode_ScrollLock);
	
	KeyMap(VK_PRINT, KeyCode_PrintScreen);
	KeyMap(VK_PAUSE, KeyCode_Pause);

	KeyMap(0x41, KeyCode_A);
	KeyMap(0x42, KeyCode_B);
	KeyMap(0x43, KeyCode_C);
	KeyMap(0x44, KeyCode_D);
	KeyMap(0x45, KeyCode_E);
	KeyMap(0x46, KeyCode_F);
	KeyMap(0x47, KeyCode_G);
	KeyMap(0x48, KeyCode_H);
	KeyMap(0x49, KeyCode_I);
	KeyMap(0x4A, KeyCode_J);
	KeyMap(0x4B, KeyCode_K);
	KeyMap(0x4C, KeyCode_L);
	KeyMap(0x4D, KeyCode_M);
	KeyMap(0x4E, KeyCode_N);
	KeyMap(0x4F, KeyCode_O);
	KeyMap(0x50, KeyCode_P);
	KeyMap(0x51, KeyCode_Q);
	KeyMap(0x52, KeyCode_R);
	KeyMap(0x53, KeyCode_S);
	KeyMap(0x54, KeyCode_T);
	KeyMap(0x55, KeyCode_U);
	KeyMap(0x56, KeyCode_V);
	KeyMap(0x57, KeyCode_W);
	KeyMap(0x58, KeyCode_X);
	KeyMap(0x59, KeyCode_Y);
	KeyMap(0x5A, KeyCode_Z);

	KeyMap(0x30, KeyCode_0);
	KeyMap(0x31, KeyCode_1);
	KeyMap(0x32, KeyCode_2);
	KeyMap(0x33, KeyCode_3);
	KeyMap(0x34, KeyCode_4);
	KeyMap(0x35, KeyCode_5);
	KeyMap(0x36, KeyCode_6);
	KeyMap(0x37, KeyCode_7);
	KeyMap(0x38, KeyCode_8);
	KeyMap(0x39, KeyCode_9);

	KeyMap(VK_OEM_4, KeyCode_OpenBracket);
	KeyMap(VK_OEM_6, KeyCode_CloseBracket);
	KeyMap(VK_OEM_1, KeyCode_Semicolon);
	KeyMap(VK_OEM_7, KeyCode_Quote);
	KeyMap(VK_OEM_COMMA, KeyCode_Comma);
	KeyMap(VK_OEM_PERIOD, KeyCode_Period);
	KeyMap(VK_OEM_2, KeyCode_Slash);
	KeyMap(VK_OEM_5, KeyCode_BackSlash);
	KeyMap(VK_OEM_3, KeyCode_Tilde);
	KeyMap(VK_OEM_PLUS, KeyCode_Plus);
	KeyMap(VK_OEM_MINUS, KeyCode_Minus);

	KeyMap(VK_NUMPAD8, KeyCode_NumUp);
	KeyMap(VK_NUMPAD2, KeyCode_NumDown);
	KeyMap(VK_NUMPAD4, KeyCode_NumLeft);
	KeyMap(VK_NUMPAD6, KeyCode_NumRight);
	KeyMap(VK_NUMPAD7, KeyCode_NumHome);
	KeyMap(VK_NUMPAD1, KeyCode_NumEnd);
	KeyMap(VK_NUMPAD9, KeyCode_NumPageUp);
	KeyMap(VK_NUMPAD3, KeyCode_NumPageDown);
	KeyMap(VK_MULTIPLY, KeyCode_NumMultiply);
	KeyMap(VK_DIVIDE, KeyCode_NumDivide);
	KeyMap(VK_ADD, KeyCode_NumPlus);
	KeyMap(VK_SUBTRACT, KeyCode_NumMinus);
	//KeyMap(VK_CLEAR, KeyCode_NumClear);
#undef KeyMap

	if (!KeyFound)
		return false;

	*OutCode = KeyCode;
	return true;
}

static void
Win32ResizeSurfaceBuffer(platform_state *PlatformState,
						win32_surface_buffer *Buffer,
						s32 NewWidth, s32 NewHeight)
{
	Assert(PlatformState);
	Assert(Buffer);
	
	if (Buffer->Pixels)
		Win32DeallocateMemory(Buffer->Pixels);
 
	const s16 BytesPerPixel = 4;
	
	Buffer->Info.bmiHeader.biSize = sizeof(Buffer->Info.bmiHeader);
	Buffer->Info.bmiHeader.biWidth = NewWidth;
	Buffer->Info.bmiHeader.biHeight = -NewHeight; // NOTE(ivan): Minus for top-left coordinate system.
	Buffer->Info.bmiHeader.biPlanes = 1;
	Buffer->Info.bmiHeader.biBitCount = BytesPerPixel * 8;
	Buffer->Info.bmiHeader.biCompression = BI_RGB;

	Buffer->Width = NewWidth;
	Buffer->Height = NewHeight;

	Buffer->BytesPerPixel = BytesPerPixel;
	Buffer->Pitch = NewWidth * BytesPerPixel;

	const u32 PixelsMemorySize = NewWidth * NewHeight * BytesPerPixel;
	if (PixelsMemorySize) {
		Buffer->Pixels = Win32AllocateMemory(PixelsMemorySize);
		if (!Buffer->Pixels)
			Win32Error(PlatformState, "Failed allocating screen buffer!");
	} else {
		Buffer->Pixels = 0;
	}

	Win32Log(PlatformState, "Screen buffer (%dx%d) created.", NewWidth, NewHeight);
}

static void
Win32DisplaySurfaceBuffer(win32_surface_buffer *Buffer,
						 HWND DestWindow,
						 HDC DestWindowDC)
{
	win32_window_dimension DestWindowDim = Win32GetWindowDimension(DestWindow);
	StretchDIBits(DestWindowDC,
				  0, 0, DestWindowDim.Width, DestWindowDim.Height,
				  0, 0, Buffer->Width, Buffer->Height,
				  Buffer->Pixels, &Buffer->Info,
				  DIB_RGB_COLORS, SRCCOPY);
}

// TODO(ivan): This is temporary fullscreen switch. Later should be implemented support of all available video modes in a host machine.
static void
Win32ToggleFullscreen(HWND Window,
					  WINDOWPLACEMENT *Placement)
{
	Assert(Placement);

	// NOTE(ivan): Big thanks for Raymond Chen from The Big New Thing blog for this routine!
	
	DWORD Style = GetWindowLong(Window, GWL_STYLE);
	if (Style & WS_OVERLAPPEDWINDOW) {
		MONITORINFO MonitorInfo = {sizeof(MonitorInfo)};
		if (GetWindowPlacement(Window, Placement) && GetMonitorInfo(MonitorFromWindow(Window, MONITOR_DEFAULTTOPRIMARY), &MonitorInfo)) {
			SetWindowLong(Window, GWL_STYLE, Style & ~WS_OVERLAPPEDWINDOW);
			SetWindowPos(Window, HWND_TOP,
						 MonitorInfo.rcMonitor.left, MonitorInfo.rcMonitor.top,
						 MonitorInfo.rcMonitor.right - MonitorInfo.rcMonitor.left,
						 MonitorInfo.rcMonitor.bottom - MonitorInfo.rcMonitor.top,
						 SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
		}
	} else {
		SetWindowLong(Window, GWL_STYLE, Style | WS_OVERLAPPEDWINDOW);
		SetWindowPlacement(Window, Placement);
		SetWindowPos(Window, 0, 0, 0, 0, 0,
					 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
					 SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
	}
}

// NOTE(ivan): Stupid WinAPI architecture forces us to do that crap: send miscellaneous structures to process specific window messages.
// Why couldn't MS allow us to just process these messages in the main loop?
struct win32_window_proc_params {
	platform_state *PlatformState;
	game_input *Input;
};

static LRESULT CALLBACK
Win32WindowProc(HWND Window,
				UINT Msg,
				WPARAM W,
				LPARAM L)
{
	static platform_state *PlatformState;
	static game_input *Input;
	
	switch (Msg) {
	case WM_CREATE: {
		CREATESTRUCT *CreateStruct = (CREATESTRUCT *)L;
		win32_window_proc_params *Params = (win32_window_proc_params *)CreateStruct->lpCreateParams;
		PlatformState = Params->PlatformState;
		Input = Params->Input;
	} break;
		
	case WM_DESTROY: {
		PostQuitMessage(0);
	} break;

	case WM_PAINT: {
		PAINTSTRUCT PaintStruct;
		HDC DC = BeginPaint(Window, &PaintStruct);
		PatBlt(DC, 0, 0, PaintStruct.rcPaint.right - PaintStruct.rcPaint.left, PaintStruct.rcPaint.bottom - PaintStruct.rcPaint.top, BLACKNESS);
		EndPaint(Window, &PaintStruct);
	} break;

	case WM_SIZE: {
		win32_window_dimension WindowDim = Win32GetWindowDimension(Window);
		Win32ResizeSurfaceBuffer(PlatformState, &PlatformState->SurfaceBuffer, WindowDim.Width, WindowDim.Height);
	} break;

	case WM_INPUT: {
		u8 Buffer[sizeof(RAWINPUT)] = {};
		u32 BufferSize = sizeof(RAWINPUT);
		GetRawInputData((HRAWINPUT)L,
						RID_INPUT,
						Buffer,
						&BufferSize,
						sizeof(RAWINPUTHEADER));
		
		RAWINPUT *RawInput = (RAWINPUT *)Buffer;
		if (RawInput->header.dwType == RIM_TYPEKEYBOARD) {
			RAWKEYBOARD &RawKeyboard = RawInput->data.keyboard;
			
			key_code KeyCode;
			if (Win32MapVKToKeyCode(RawKeyboard.VKey,
									RawKeyboard.MakeCode,
									(RawKeyboard.Flags & RI_KEY_E0) != 0,
									(RawKeyboard.Flags & RI_KEY_E1) != 0,
									&KeyCode))
				Win32ProcessKeyboardOrMouseButton(&Input->KeyboardButtons[KeyCode], (RawKeyboard.Flags & RI_KEY_BREAK) == 0);
		}
	} break;
		
	default: {
		if (PlatformState && Msg && Msg == PlatformState->QueryCancelAutoplay)
			return TRUE; // NOTE(ivan): Cancel CD-ROM autoplay.
		return DefWindowProcA(Window, Msg, W, L);
	} break;
	}

	return 0;
}

int CALLBACK
WinMain(HINSTANCE Instance,
		HINSTANCE PrevInstance,
		LPSTR CommandLine,
		int ShowCommand)
{
	UnreferencedParam(PrevInstance);
	UnreferencedParam(CommandLine);

	platform_state PlatformState = {};
	PlatformState.Running = true;

	PlatformState.Instance = Instance;
	PlatformState.ShowCommand = ShowCommand;

	// TODO(ivan): Are these UTF-8 or ANSI?
	PlatformState.Params = __argv;
	PlatformState.NumParams = __argc;

#if INTERNAL
	PlatformState.DebugCursor = true;
#endif

	// NOTE(ivan): Set primary thread name.
	Win32SetThreadName(GetCurrentThreadId(), GAMENAME " primary thread");

	// NOTE(ivan): Check OS version.
	if (!IsWindows7OrGreater())
		Win32Error(&PlatformState, GAMENAME " requires Windows 7 or newer OS!");

	// NOTE(ivan): Check if already running.
	HANDLE AlreadyRunning = CreateMutexA(0, TRUE, GAMENAME "AlreadyRunning");
	if (!AlreadyRunning && GetLastError() == ERROR_ALREADY_EXISTS)
		Win32Error(&PlatformState, GAMENAME " is already running!");

	// NOTE(ivan): Query CD-ROM autorun disable.
	PlatformState.QueryCancelAutoplay = RegisterWindowMessageA("QueryCancelAutoplay");

	// NOTE(ivan): Query executable's file name & path.
	char ModuleName[MAX_PATH + 1] = {};
	Verify(GetModuleFileNameA(PlatformState.Instance, ModuleName, CountOf(ModuleName)));

	char *PastLastSlash = ModuleName, *Ptr = ModuleName;
	while (*Ptr) {
		if (*Ptr == '\\') // NOTE(ivan): GetModuleFileNameA() always returns path with DOS path separators.
			PastLastSlash = Ptr + 1;
		Ptr++;
	}
	strncpy(PlatformState.ExeName, PastLastSlash, CountOf(PlatformState.ExeName) - 1);
	strncpy(PlatformState.ExePath, ModuleName, PastLastSlash - ModuleName);

	strcpy(PlatformState.ExeNameNoExt, PlatformState.ExeName);
	char *LastDot = PlatformState.ExeNameNoExt;
	for (Ptr = PlatformState.ExeNameNoExt; *Ptr; Ptr++) {
		if (*Ptr == '.')
			LastDot = Ptr;
	}
	*LastDot = 0;

	// NOTE(ivan): Query performance frequency.
	LARGE_INTEGER PerformanceFrequency;
	Verify(QueryPerformanceFrequency(&PerformanceFrequency));
	PlatformState.PerformanceFrequency = PerformanceFrequency.QuadPart;

	// NOTE(ivan): Set sleep granularity.
	b32 IsSleepGranular = (timeBeginPeriod(1) != TIMERR_NOCANDO);

	// NOTE(ivan): Change current directory if requested.
	const char *WorkDir = Win32CheckParamValue(&PlatformState, "-cwd");
	if (WorkDir)
		SetCurrentDirectoryA(WorkDir);
	
	// NOTE(ivan): Initialize log file.
#if INTERNAL	
	char LogName[MAX_PATH + 1] = {};
	snprintf(LogName, CountOf(LogName) - 1, "%s.log", PlatformState.ExeNameNoExt);

	PlatformState.LogFile = CreateFileA(LogName, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (PlatformState.LogFile != INVALID_HANDLE_VALUE) {
		u8 UnicodeBOM[] = {0xEF, 0xBB, 0xBF}; // NOTE(ivan): UTF-8.
		DWORD Unused;
		WriteFile(PlatformState.LogFile, UnicodeBOM, sizeof(UnicodeBOM), &Unused, 0);
	}
#endif	

	// NOTE(ivan): Initialize COM.
	Verify(SUCCEEDED(CoInitializeEx(0, COINIT_MULTITHREADED)));

	// NOTE(ivan): Initialize work queues for multithreading.
	Win32InitializeWorkQueue(&PlatformState, &PlatformState.HighPriorityWorkQueue, 6);
	Win32InitializeWorkQueue(&PlatformState, &PlatformState.LowPriorityWorkQueue, 2);

	// NOTE(ivan): Initialize platform API structure.
	platform_api PlatformAPI = {};
	PlatformAPI.CheckParamValue = Win32CheckParamValue;
	PlatformAPI.CheckParam = Win32CheckParam;
	PlatformAPI.Log = Win32Log;
	PlatformAPI.Error = Win32Error;
	PlatformAPI.AllocateMemory = Win32AllocateMemory;
	PlatformAPI.DeallocateMemory = Win32DeallocateMemory;
	PlatformAPI.GetMemoryStats = Win32GetMemoryStats;
	PlatformAPI.AddWorkQueueEntry = Win32AddWorkQueueEntry;
	PlatformAPI.CompleteWorkQueue = Win32CompleteWorkQueue;
	PlatformAPI.ReadEntireFile = Win32ReadEntireFile;
	PlatformAPI.FreeEntireFileMemory = Win32FreeEntireFileMemory;
	PlatformAPI.WriteEntireFile = Win32WriteEntireFile;

	PlatformAPI.HighPriorityWorkQueue = &PlatformState.HighPriorityWorkQueue;
	PlatformAPI.LowPriorityWorkQueue = &PlatformState.LowPriorityWorkQueue;

	// NOTE(ivan): Initialize input structure for future use.
	game_input Input = {};

	// NOTE(ivan): Create main window & its DC.
	WNDCLASSA WindowClass = {};
	WindowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	WindowClass.lpszClassName = GAMENAME "PrimaryWindow";
	WindowClass.lpfnWndProc = Win32WindowProc;
	WindowClass.hInstance = PlatformState.Instance;
	
	Verify(RegisterClass(&WindowClass));

	win32_window_proc_params WindowProcParams = {&PlatformState, &Input};
	PlatformState.MainWindow = CreateWindowExA(WS_EX_APPWINDOW,
											   WindowClass.lpszClassName,
											   GAMENAME,
											   WS_OVERLAPPEDWINDOW,
											   CW_USEDEFAULT, CW_USEDEFAULT,
											   CW_USEDEFAULT, CW_USEDEFAULT,
											   0, 0,
											   Instance,
											   &WindowProcParams);
	Assert(PlatformState.MainWindow);

	PlatformState.MainWindowPlacement.length = sizeof(PlatformState.MainWindowPlacement);
	Verify(GetWindowPlacement(PlatformState.MainWindow, &PlatformState.MainWindowPlacement));

	PlatformState.MainWindowDC = GetDC(PlatformState.MainWindow);
	Assert(PlatformState.MainWindowDC);

	// NOTE(ivan): Initialize raw keyboard input handling.
	RAWINPUTDEVICE KeyboardDevice;
	KeyboardDevice.usUsagePage = 0x01;
	KeyboardDevice.usUsage = 0x06;
	KeyboardDevice.dwFlags = RIDEV_NOLEGACY;
	KeyboardDevice.hwndTarget = PlatformState.MainWindow;
	
   	Verify(RegisterRawInputDevices(&KeyboardDevice, 1, sizeof(KeyboardDevice)));

	// NOTE(ivan): Initialize controller input handling.
	PlatformState.XInput = Win32LoadXInput(&PlatformState);

	// NOTE(ivan): Initialize game state structure.
	game_state *State = (game_state *)Win32AllocateMemory(sizeof(game_state));
	if (!State)
		Win32Error(&PlatformState, "Failed allocating game state!");
	memset(State, 0, sizeof(game_state)); // NOTE(ivan): Game state MUST be zeroed.
	State->Type = GameStateType_Init;

	// NOTE(ivan): Show main window after all initialization is done.
	ShowWindow(PlatformState.MainWindow, PlatformState.ShowCommand);
#if !INTERNAL
	Win32ToggleFullscreen(PlatformState.MainWindow, &PlatformState.MainWindowPlacement);
#endif

	// NOTE(ivan): Setup timings right before primary cycle.
	game_clocks Clocks = {};
	u64 LastCycleCounter = Win32GetClock();
	u64 LastFPSCounter = Win32GetClock();
	u32 NumFrames = 0;

	// NOTE(ivan): Primary cycle.
	MSG Msg = {};
	while (PlatformState.Running) {
		// NOTE(ivan): Reset keyboard half-transition counters.
		for (u32 ButtonIndex = 0; ButtonIndex < CountOf(Input.KeyboardButtons); ButtonIndex++)
			Input.KeyboardButtons[ButtonIndex].HalfTransitionCount = 0;
		
		// NOTE(ivan): Process OS messages.
		while (PeekMessageA(&Msg, 0, 0, 0, PM_REMOVE)) {
			if (Msg.message == WM_QUIT)
				PlatformState.Running = false;
			
			TranslateMessage(&Msg);
			DispatchMessageA(&Msg);
		}
		
		// NOTE(ivan): Process XInput controller state.
		// TODO(ivan): Monitor XInput controllers for being plugged in after the fact!
		b32 XBoxControllerPresent[XUSER_MAX_COUNT];
		for (u32 ControllerIndex = 0; ControllerIndex < XUSER_MAX_COUNT; ControllerIndex++)
			XBoxControllerPresent[ControllerIndex] = true;

		// TODO(ivan): Need to not poll disconnected controllers to avoid XInput frame rate hit on older libraries...
		// TODO(ivan): Should we poll this more frequently?
		DWORD MaxControllerCount = XUSER_MAX_COUNT;
		if (MaxControllerCount > CountOf(Input.Controllers))
			MaxControllerCount = CountOf(Input.Controllers);
		for (u32 ControllerIndex = 0; ControllerIndex < MaxControllerCount; ControllerIndex++) {
			game_input_controller *Controller = &Input.Controllers[ControllerIndex];
			XINPUT_STATE ControllerState;
			if (XBoxControllerPresent[ControllerIndex] && PlatformState.XInput.GetState(ControllerIndex, &ControllerState) == ERROR_SUCCESS) {
				Controller->IsConnected = true;
				
				// TODO(ivan): See if ControllerState.dwPacketNumber increments too rapidly.
				XINPUT_GAMEPAD *Gamepad = &ControllerState.Gamepad;

				Win32ProcessXInputDigitalButton(&Controller->Start,
												Gamepad->wButtons,
												XINPUT_GAMEPAD_START);
				Win32ProcessXInputDigitalButton(&Controller->Back,
												Gamepad->wButtons,
												XINPUT_GAMEPAD_BACK);

				Win32ProcessXInputDigitalButton(&Controller->A,
												Gamepad->wButtons,
												XINPUT_GAMEPAD_A);
				Win32ProcessXInputDigitalButton(&Controller->B,
												Gamepad->wButtons,
												XINPUT_GAMEPAD_B);
				Win32ProcessXInputDigitalButton(&Controller->X,
												Gamepad->wButtons,
												XINPUT_GAMEPAD_X);
				Win32ProcessXInputDigitalButton(&Controller->Y,
												Gamepad->wButtons,
												XINPUT_GAMEPAD_Y);
				
				Win32ProcessXInputDigitalButton(&Controller->Up,
												Gamepad->wButtons,
												XINPUT_GAMEPAD_DPAD_UP);
				Win32ProcessXInputDigitalButton(&Controller->Down,
												Gamepad->wButtons,
												XINPUT_GAMEPAD_DPAD_DOWN);
				Win32ProcessXInputDigitalButton(&Controller->Left,
												Gamepad->wButtons,
												XINPUT_GAMEPAD_DPAD_LEFT);
				Win32ProcessXInputDigitalButton(&Controller->Right,
												Gamepad->wButtons,
												XINPUT_GAMEPAD_DPAD_RIGHT);

				Win32ProcessXInputDigitalButton(&Controller->LeftBumper,
												Gamepad->wButtons,
												XINPUT_GAMEPAD_LEFT_SHOULDER);
				Win32ProcessXInputDigitalButton(&Controller->RightBumper,
												Gamepad->wButtons,
												XINPUT_GAMEPAD_RIGHT_SHOULDER);

				Win32ProcessXInputDigitalButton(&Controller->LeftStick,
												Gamepad->wButtons,
												XINPUT_GAMEPAD_LEFT_THUMB);
				Win32ProcessXInputDigitalButton(&Controller->RightStick,
												Gamepad->wButtons,
												XINPUT_GAMEPAD_RIGHT_THUMB);

				Controller->LeftTrigger = Gamepad->bLeftTrigger;
				Controller->RightTrigger = Gamepad->bRightTrigger;

				Controller->LeftStickX = Win32ProcessXInputStickValue(Gamepad->sThumbLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
				Controller->LeftStickY = Win32ProcessXInputStickValue(Gamepad->sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
				Controller->RightStickX = Win32ProcessXInputStickValue(Gamepad->sThumbRX, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
				Controller->RightStickY = Win32ProcessXInputStickValue(Gamepad->sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
			} else {
				Controller->IsConnected = false;
			}
		}

		// NOTE(ivan): Capture mouse state.
		POINT MousePos;
		GetCursorPos(&MousePos);
		ScreenToClient(PlatformState.MainWindow, &MousePos);
		Input.MouseX = MousePos.x;
		Input.MouseY = MousePos.y;
		for(u32 MouseButtonIndex = 0; MouseButtonIndex < CountOf(Input.MouseButtons); MouseButtonIndex++) {
			static DWORD WinButtonId[5] = {VK_LBUTTON, VK_MBUTTON, VK_RBUTTON, VK_XBUTTON1, VK_XBUTTON2};
			game_input_button *MouseButton = &Input.MouseButtons[MouseButtonIndex];
			b32 IsDown = (GetKeyState(WinButtonId[MouseButtonIndex]) & (1 << 15)); // TODO(ivan): GetAsyncKeyState()?

			Win32ProcessKeyboardOrMouseButton(MouseButton, IsDown);
		}

		// NOTE(ivan): Process Win32-side input events.
		if (Input.KeyboardButtons[KeyCode_F4].IsDown && Input.KeyboardButtons[KeyCode_LeftAlt].IsDown)
			PlatformState.Running = false;
#if INTERNAL		
		if (IsSinglePress(Input.KeyboardButtons[KeyCode_F2]))
			PlatformState.DebugCursor = !PlatformState.DebugCursor;
#endif
		if (Input.KeyboardButtons[KeyCode_LeftAlt].IsDown && IsSinglePress(Input.KeyboardButtons[KeyCode_Enter]))
			Win32ToggleFullscreen(PlatformState.MainWindow, &PlatformState.MainWindowPlacement);

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
				   State);

		// NOTE(ivan): Display offscreen graphics buffer.
		Win32DisplaySurfaceBuffer(&PlatformState.SurfaceBuffer,
								 PlatformState.MainWindow,
								 PlatformState.MainWindowDC);
		
		// NOTE(ivan): Set cursor.
		if (PlatformState.DebugCursor)
			SetCursor(LoadCursorA(0, MAKEINTRESOURCEA(32515))); // NOTE(ivan): IDC_CROSS.
		else
			SetCursor(0);

		// NOTE(ivan): Make keyboard input data obsolete for next frame.
		for (u32 ButtonIndex = 0; ButtonIndex < CountOf(Input.KeyboardButtons); ButtonIndex++)
			Input.KeyboardButtons[ButtonIndex].IsActual = false;

		// NOTE(ivan): Quit if requested.
		if (PlatformAPI.QuitRequested)
			PlatformState.Running = false;

		// NOTE(ivan): Finish timings.
		u64 EndCycleCounter = Win32GetClock();
		u64 EndFPSCounter = Win32GetClock();

		Clocks.SecondsPerFrame = Win32GetSecondsElapsed(LastCycleCounter, EndCycleCounter, PlatformState.PerformanceFrequency);
		LastCycleCounter = EndCycleCounter;

		f64 SecondsElapsed = Win32GetSecondsElapsed(LastFPSCounter, EndFPSCounter, PlatformState.PerformanceFrequency);
		if (SecondsElapsed >= 1.0) {
			LastFPSCounter = EndFPSCounter;
			Clocks.FramesPerSecond = NumFrames;
			NumFrames = 0;
		} else {
			NumFrames++;
		}

		// NOTE(ivan): Now next frame won't be the first one again!
		State->Type = GameStateType_Frame;
	}

	// NOTE(ivan): Game uninitialization.
	State->Type = GameStateType_Shutdown;
	UpdateGame(&PlatformState,
			   &PlatformAPI,
			   0,
			   0,
			   0,
			   State);

	// NOTE(ivan): Destroy game state.
	Win32DeallocateMemory(State);

	// NOTE(ivan): Destroy keyboard raw input processing.
	KeyboardDevice.usUsagePage = 0x01;
	KeyboardDevice.usUsage = 0x06;
	KeyboardDevice.dwFlags = RIDEV_REMOVE;
	KeyboardDevice.hwndTarget = PlatformState.MainWindow;
		
	RegisterRawInputDevices(&KeyboardDevice, 1, sizeof(KeyboardDevice));

	// NOTE(ivan): Unload XInput library.
	Win32FreeXInput(&PlatformState.XInput);

	// NOTE(ivan): Release work queues.
	Win32ReleaseWorkQueue(&PlatformState.HighPriorityWorkQueue);
	Win32ReleaseWorkQueue(&PlatformState.LowPriorityWorkQueue);

	// NOTE(ivan): Release graphics subsystem.
	if (PlatformState.SurfaceBuffer.Pixels)
		Win32DeallocateMemory(PlatformState.SurfaceBuffer.Pixels);
	ReleaseDC(PlatformState.MainWindow, PlatformState.MainWindowDC);
	DestroyWindow(PlatformState.MainWindow);
	UnregisterClass(WindowClass.lpszClassName, PlatformState.Instance);

	// NOTE(ivan): Restore Sleep() granularity.
	if (IsSleepGranular)
		timeEndPeriod(1);

	// NOTE(ivan): Finish log file.
#if INTERNAL	
	if (PlatformState.LogFile != INVALID_HANDLE_VALUE)
		CloseHandle(PlatformState.LogFile);
#endif
	
	// NOTE(ivan): Finish COM.
	CoUninitialize();

	// NOTE(ivan): Finish AlreadyRunning global mutex.
	CloseHandle(AlreadyRunning);
	
	return (int)Msg.wParam;
}





























