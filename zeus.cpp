#include "zeus.h"
#include "zeus_platform.h"
#include "zeus_memory.h"
#include "zeus_math.h"
#include "zeus_misc.h"
#include "zeus_draw.h"
#include "zeus_image.h"
#if WIN32
#include "zeus_platform_win32.cpp"
#else
#error "Unsupported target platform!"
#endif
#include "zeus_memory.cpp"
#include "zeus_misc.cpp"
#include "zeus_draw.cpp"
#include "zeus_image.cpp"

inline void
PushConfigurationEntry(platform_state *PlatformState,
					   platform_api *PlatformAPI,
					   game_config *Config,
					   const char *Name,
					   const char *Value)
{
	Assert(Config);
	Assert(Name);
	Assert(Value);

	// NOTE(ivan): If already exists - change its value.
	for (game_config_entry *Entry = Config->LastEntry; Entry; Entry = Entry->Prev) {
		if (strcmp(Entry->Name, Name) == 0) {
			memset(Entry->Value, 0, CountOf(Entry->Value));
			strncpy(Entry->Value, Value, CountOf(Entry->Value) - 1);
			return;
		}
	}

	// NOTE(ivan): Insert new entry.
	game_config_entry *NewEntry = PushStackType(PlatformState, PlatformAPI,
												&Config->ConfigStack, game_config_entry);
	if (NewEntry) {
		memset(NewEntry, 0, sizeof(game_config_entry));
		strncpy(NewEntry->Name, Name, CountOf(NewEntry->Name) - 1);
		strncpy(NewEntry->Value, Value, CountOf(NewEntry->Value) - 1);
		
		NewEntry->Next = 0;
		NewEntry->Prev = Config->LastEntry;
		if (Config->LastEntry)
			Config->LastEntry->Next = NewEntry;
		
		Config->LastEntry = NewEntry;
	}
}

game_config
LoadConfiguration(platform_state *PlatformState,
				  platform_api *PlatformAPI,
				  const char *FileName,
				  game_config *Config)
{
	Assert(PlatformAPI);
	Assert(FileName);
	
	game_config Result = {};

	PlatformAPI->Log(PlatformState, "Loading configuration-file '%s'...", FileName);

	InitializeMemoryStack(PlatformState,
						  PlatformAPI,
						  &Result.ConfigStack,
						  "ConfigStack", // TODO(ivan): Find a better name for a config stack?
						  sizeof(game_config_entry),
						  sizeof(game_config_entry) * 64);

	// NOTE(ivan): Read data from parent cache.
	if (Config) {
		EnterTicketMutex(&Config->ConfigMutex);
		for (game_config_entry *Entry = Config->LastEntry; Entry; Entry = Entry->Prev)
			PushConfigurationEntry(PlatformState, PlatformAPI,
								   &Result, Entry->Name, Entry->Value);
		LeaveTicketMutex(&Config->ConfigMutex);
		FreeConfiguration(PlatformAPI, Config);
	}
	
	// NOTE(ivan): Read data from file.
	piece ReadResult = PlatformAPI->ReadEntireFile(FileName);
	if (ReadResult.Memory) {
		char Line[1024];
		u32 LinePos = 0;
		while (GetLine((char *)ReadResult.Memory, &LinePos, Line, CountOf(Line))) {
			tokenize_string_result TokenizeResult = TokenizeString(PlatformAPI, Line, " \t");
			if (TokenizeResult.Tokens) {
				if (TokenizeResult.NumTokens >= 2)
					PushConfigurationEntry(PlatformState, PlatformAPI,
										   &Result, TokenizeResult.Tokens[0], TokenizeResult.Tokens[1]);
				
				FreeTokenizeResult(PlatformAPI, &TokenizeResult);
			}
		}
		
		PlatformAPI->FreeEntireFileMemory(&ReadResult);
	}

	return Result;
}
		
void
SaveConfiguration(platform_state *PlatformState,
				  platform_api *PlatformAPI,
				  const char *FileName,
				  game_config *Config)
{
	Assert(PlatformState);
	Assert(PlatformAPI);
	Assert(FileName);
	Assert(Config);

	PlatformAPI->Log(PlatformState, "Saving configuration-file '%s'...", FileName);

	EnterTicketMutex(&Config->ConfigMutex);

	// NOTE(ivan): We need to write entries in order from the first one to the last one loaded.
	game_config_entry *FirstEntry = Config->LastEntry;
	while (FirstEntry) {
		if (!FirstEntry->Prev)
			break;
		FirstEntry = FirstEntry->Prev;
	}

	char Buffer[4096] = {}; // TODO(ivan): dynamically allocate the buffer OR use openfile/writefile/closefile eventually.
	u32 Pos = 0;
	game_config_entry *Entry = FirstEntry;
	while (Entry) {
		Pos += snprintf(Buffer + Pos, CountOf(Buffer) - Pos - 1, "%s %s\n", Entry->Name, Entry->Value);
		Entry = Entry->Next;
	}

	if (Pos)
		PlatformAPI->WriteEntireFile(FileName, Buffer, Pos);

	LeaveTicketMutex(&Config->ConfigMutex);
}

void
FreeConfiguration(platform_api *PlatformAPI, game_config *Config)
{
	Assert(Config);

	EnterTicketMutex(&Config->ConfigMutex);
	
	FreeMemoryStack(PlatformAPI, &Config->ConfigStack);
	Config->LastEntry = 0;

	LeaveTicketMutex(&Config->ConfigMutex);
}

void
UpdateGame(platform_state *PlatformState,
		   platform_api *PlatformAPI,
		   game_clocks *Clocks,
		   game_surface_buffer *SurfaceBuffer,
		   game_input *Input,
		   game_state *State)
{
	UnreferencedParam(Clocks);
	UnreferencedParam(SurfaceBuffer);
	UnreferencedParam(Input);
	
	switch (State->Type) {
		///////////////////////////////////////////////////////////////////////////////////////////////////
		// NOTE(ivan): Game initialization.
		///////////////////////////////////////////////////////////////////////////////////////////////////
	case GameStateType_Init: {
		InitializeMemoryStack(PlatformState,
							  PlatformAPI,
							  &State->FrameStack,
							  "FrameStack",
							  0, 1024);
		
		// NOTE(ivan): Load configuration.
		State->Config = LoadConfiguration(PlatformState, PlatformAPI, "default.cfg", 0);
		State->Config = LoadConfiguration(PlatformState, PlatformAPI, "user.cfg", &State->Config);

		State->TestImage = LoadBMP(PlatformState, PlatformAPI, "data\\hhtest.bmp");

		State->XOffset = 0;
		State->YOffset = 0;
		
		State->PosX = 0.5f;
		State->PosY = 0.5f;
	} break;

		///////////////////////////////////////////////////////////////////////////////////////////////////
		// NOTE(ivan): Game frame.
		///////////////////////////////////////////////////////////////////////////////////////////////////
	case GameStateType_Frame: {
		if (Input->KeyboardButtons[KeyCode_W].IsDown)
			State->PosY -= 0.001f;
		if (Input->KeyboardButtons[KeyCode_S].IsDown)
			State->PosY += 0.001f;
		if (Input->KeyboardButtons[KeyCode_A].IsDown)
			State->PosX -= 0.001f;
		if (Input->KeyboardButtons[KeyCode_D].IsDown)
			State->PosX += 0.001f;
		
        DrawSolidColor(SurfaceBuffer, MakeRGBA(0.0f, 0.0f, 0.0f, 1.0f));
		
		DrawWeirdGradient(SurfaceBuffer, State->XOffset, State->YOffset);
		State->XOffset++;
		State->YOffset++;

		DrawImage(SurfaceBuffer, MakeV2(State->PosX, State->PosY), &State->TestImage);

		// NOTE(ivan): Free per-frame memory arena.
		FreeMemoryStack(PlatformAPI, &State->FrameStack);
	} break;

		///////////////////////////////////////////////////////////////////////////////////////////////////
		// NOTE(ivan): Game release.
		///////////////////////////////////////////////////////////////////////////////////////////////////
	case GameStateType_Shutdown: {
		FreeImage(PlatformAPI, &State->TestImage);
		
		// NOTE(ivan): Save configuration.
		SaveConfiguration(PlatformState, PlatformAPI, "user.cfg", &State->Config);
		FreeConfiguration(PlatformAPI, &State->Config);
	} break;
	}
}
