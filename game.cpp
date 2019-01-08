#include "game.h"
#include "game_platform.h"
#include "game_memory.h"
#include "game_math.h"
#include "game_misc.h"
#include "game_draw.h"
#include "game_draw_group.h"
#include "game_image.h"
#if WIN32
#include "game_platform_win32.cpp"
#elif LINUX
#include "game_platform_linux.cpp"
#else
#error "Unsupported target platform!"
#endif
#include "game_memory.cpp"
#include "game_misc.cpp"
#include "game_draw.cpp"
#include "game_draw_group.cpp"
#include "game_image.cpp"

// NOTE(ivan): Capacity of draw group buffer.
#define MAX_DRAW_GROUP_BUFFER 2048

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

LOAD_CONFIGURATION(LoadConfiguration)
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
		
SAVE_CONFIGURATION(SaveConfiguration)
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

FREE_CONFIGURATION(FreeConfiguration)
{
	Assert(Config);

	EnterTicketMutex(&Config->ConfigMutex);
	
	FreeMemoryStack(PlatformAPI, &Config->ConfigStack);
	Config->LastEntry = 0;

	LeaveTicketMutex(&Config->ConfigMutex);
}

GET_CONFIGURATION_VALUE(GetConfigurationValue)
{
	Assert(Config);
	Assert(Name);
	Assert(Default);

	EnterTicketMutex(&Config->ConfigMutex);

	for (game_config_entry *Entry = Config->LastEntry; Entry; Entry = Entry->Prev) {
		if (strcmp(Entry->Name, Name) == 0) {
			LeaveTicketMutex(&Config->ConfigMutex);
			return Entry->Value;
		}
	}

	LeaveTicketMutex(&Config->ConfigMutex);
	return Default;
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

	static game_api GameAPI = {};

	static char EntitiesFileName[1024] = {};
	static char EntitiesTempFileName[1024] = {};
	static char EntitiesLockFileName[1024] = {};

	static image TestImage = {};
	
	switch (State->Type) {
		///////////////////////////////////////////////////////////////////////////////////////////////////
		// NOTE(ivan): Game initialization.
		///////////////////////////////////////////////////////////////////////////////////////////////////
	case GameStateType_Prepare: {
		// NOTE(ivan): Prepare game interface.
		GameAPI.PlatformState = PlatformState;
		GameAPI.PlatformAPI = PlatformAPI;
		GameAPI.GameState = State;
		GameAPI.LoadConfiguration = LoadConfiguration;
		GameAPI.SaveConfiguration = SaveConfiguration;
		GameAPI.FreeConfiguration = FreeConfiguration;
		GameAPI.GetConfigurationValue = GetConfigurationValue;
		GameAPI.PushDrawGroupRectangle = PushDrawGroupRectangle;
		GameAPI.PushDrawGroupImage = PushDrawGroupImage;

		// NOTE(ivan): Initialize per-frame stack.
		InitializeMemoryStack(PlatformState,
							  PlatformAPI,
							  &State->FrameStack,
							  "FrameStack",
							  0, 1024);
		
		// NOTE(ivan): Load configuration.
		State->Config = LoadConfiguration(PlatformState, PlatformAPI, "default.cfg", 0);
		State->Config = LoadConfiguration(PlatformState, PlatformAPI, "user.cfg", &State->Config);

		// NOTE(ivan): Load entities.
		snprintf(EntitiesFileName, CountOf(EntitiesFileName) - 1, "%s%s_ents" DLL_EXTENSION, PlatformAPI->ExePath, PlatformAPI->ExeNameNoExt);
		snprintf(EntitiesTempFileName, CountOf(EntitiesFileName) - 1, "%s%s_ents.tmp", PlatformAPI->ExePath, PlatformAPI->ExeNameNoExt);
		snprintf(EntitiesLockFileName, CountOf(EntitiesFileName) - 1, "%s%s_ents.lck", PlatformAPI->ExePath, PlatformAPI->ExeNameNoExt);
		PlatformAPI->ReloadEntitiesModule(PlatformState, EntitiesFileName, EntitiesTempFileName, EntitiesLockFileName, &GameAPI);

		TestImage = LoadBMP(PlatformState,
							PlatformAPI,
							"data/hhtest.bmp");
	} break;

		///////////////////////////////////////////////////////////////////////////////////////////////////
		// NOTE(ivan): Game frame.
		///////////////////////////////////////////////////////////////////////////////////////////////////
	case GameStateType_Frame: {
#if INTERNAL
		// NOTE(ivan): Reload entities if necessary.
		if (PlatformAPI->ShouldEntitiesModuleBeReloaded(PlatformState, EntitiesFileName))
			PlatformAPI->ReloadEntitiesModule(PlatformState, EntitiesFileName, EntitiesTempFileName, EntitiesLockFileName, &GameAPI);
#endif

		// NOTE(ivan): Prepare draw group.
		draw_group *PrimaryDrawGroup = PushStackType(PlatformState,
													 PlatformAPI,
													 &State->FrameStack,
													 draw_group);
		draw_basis DefaultBasis = {0, 0};
		PrimaryDrawGroup->DefaultBasis = &DefaultBasis;
		PrimaryDrawGroup->EntriesBase = (u8 *)PushStackSize(PlatformState,
															PlatformAPI,
															&State->FrameStack,
															MAX_DRAW_GROUP_BUFFER);
		PrimaryDrawGroup->EntriesBytes = 0;
		PrimaryDrawGroup->EntriesMax = MAX_DRAW_GROUP_BUFFER;
		
		// NOTE(ivan): Clear surface buffer.
        DrawRectangle(SurfaceBuffer,
					  MakeV2(0.0f, 0.0f),
					  MakeV2((f32)(SurfaceBuffer->Width - 1), (f32)(SurfaceBuffer->Height - 1)),
					  MakeRGBA(0.0f, 0.0f, 0.0f, 1.0f));

		PushDrawGroupImage(PrimaryDrawGroup, MakeV2(20.0f, 20.0f), &TestImage);
		
		// NOTE(ivan): Present draw group to the surface buffer.
		DrawGroup(PrimaryDrawGroup, SurfaceBuffer);

		// NOTE(ivan): Free per-frame stack.
		FreeMemoryStack(PlatformAPI, &State->FrameStack);
		
	} break;

		///////////////////////////////////////////////////////////////////////////////////////////////////
		// NOTE(ivan): Game release.
		///////////////////////////////////////////////////////////////////////////////////////////////////
	case GameStateType_Release: {
		FreeImage(PlatformAPI, &TestImage);
		
		// NOTE(ivan): Save configuration.
		SaveConfiguration(PlatformState, PlatformAPI, "user.cfg", &State->Config);
		FreeConfiguration(PlatformAPI, &State->Config);
	} break;
	}
}
