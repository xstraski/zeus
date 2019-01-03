#include "game.h"
#include "game_platform.h"
#include "game_memory.h"
#include "game_math.h"
#include "game_misc.h"
#include "game_draw.h"
#include "game_image.h"
#if WIN32
#include "game_platform_win32.cpp"
#else
#error "Unsupported target platform!"
#endif
#include "game_memory.cpp"
#include "game_misc.cpp"
#include "game_draw.cpp"
#include "game_image.cpp"

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

REG_ENTITY_DESC(RegEntityDesc)
{
	Assert(PlatformState);
	Assert(PlatformAPI);
	Assert(GameState);
	Assert(Name);
	Assert(StateBytes);
	Assert(UpdateGameEntity);

	EnterTicketMutex(&GameState->EntityMutex);

	// NOTE(ivan): Check whether is already registered.
	for (game_entity_desc *Desc = GameState->EntitiesDesc; Desc; Desc = Desc->Next) {
		if (strcmp(Desc->Name, Name) == 0) {
			LeaveTicketMutex(&GameState->EntityMutex);
			return;
		}
	}

	// NOTE(ivan): Register entity desc.
	PlatformAPI->Log(PlatformState, "Registering entity-desc [%s]...", Name);

	game_entity_desc *NewDesc = PushPoolType(PlatformState,
											 PlatformAPI,
											 &GameState->EntitiesDescPool,
											 game_entity_desc);
	if (!NewDesc) {
		LeaveTicketMutex(&GameState->EntityMutex);
		PlatformAPI->Error(PlatformState, "Cannot register, out of memory!");
	}

	memset(NewDesc, 0, sizeof(game_entity_desc));
	strncpy(NewDesc->Name, Name, CountOf(NewDesc->Name) - 1);
	NewDesc->StateBytes = StateBytes;
	NewDesc->UpdateGameEntity = UpdateGameEntity;

	NewDesc->Next = GameState->EntitiesDesc;
	GameState->EntitiesDesc = NewDesc;

	LeaveTicketMutex(&GameState->EntityMutex);
}

SPAWN_ENTITY(SpawnEntity)
{
	Assert(PlatformState);
	Assert(PlatformAPI);
	Assert(GameState);
	Assert(Name);

	EnterTicketMutex(&GameState->EntityMutex);

	for (game_entity_desc *Desc = GameState->EntitiesDesc; Desc; Desc = Desc->Next) {
		if (strcmp(Desc->Name, Name) == 0) {
			PlatformAPI->Log(PlatformState, "Spawning entity [%s:%d]...", Name, GameState->NextEntityId);
			
			game_entity *NewEntity = PushPoolType(PlatformState,
												  PlatformAPI,
												  &GameState->EntitiesPool,
												  game_entity);
			if (!NewEntity) {
				LeaveTicketMutex(&GameState->EntityMutex);
				PlatformAPI->Error(PlatformState, "Cannot spawn, out of memory!");
			}

			memset(NewEntity, 0, sizeof(game_entity));
			strncpy(NewEntity->Name, Name, CountOf(NewEntity->Name) - 1);
			NewEntity->Id = GameState->NextEntityId++;
			NewEntity->State = PlatformAPI->AllocateMemory(Desc->StateBytes);
			if (!NewEntity->State) {
				LeaveTicketMutex(&GameState->EntityMutex);
				PlatformAPI->Error(PlatformState, "Cannot spawn, out of memory!");
			}
			NewEntity->UpdateGameEntity = Desc->UpdateGameEntity;
			NewEntity->Pos = Pos;

			NewEntity->Next = GameState->Entities;
			if (GameState->Entities)
				GameState->Entities->Prev = NewEntity;
			GameState->Entities = NewEntity;

			NewEntity->UpdateGameEntity(GameStateType_Prepare, NewEntity->State);
			
			break;
		}
	}

	LeaveTicketMutex(&GameState->EntityMutex);
}

KILL_ENTITY(KillEntity)
{
	Assert(PlatformAPI);
	Assert(GameState);

	EnterTicketMutex(&GameState->EntityMutex);

	for (game_entity *Entity = GameState->Entities; Entity; Entity = Entity->Next) {
		if (Entity->Id == Id) {
			Entity->UpdateGameEntity(GameStateType_Release, Entity->State);

			if (!Entity->Next && !Entity->Prev) {
				GameState->Entities = 0;
			} else {
				if (Entity->Next)
					Entity->Next->Prev = Entity->Prev;
				if (Entity->Prev)
					Entity->Prev->Next = Entity->Next;
			}

			PlatformAPI->DeallocateMemory(Entity->State);
			
			FreePoolType(&GameState->EntitiesPool,
						 Entity);

			break;
		}
	}

	LeaveTicketMutex(&GameState->EntityMutex);
}

static void
KillAllEntities(platform_api *PlatformAPI,
				game_state *GameState)
{
	Assert(GameState);

	EnterTicketMutex(&GameState->EntityMutex);
	
	game_entity *Entity = GameState->Entities;
	while (Entity) {
		game_entity *EntityToDelete = Entity;
		Entity = Entity->Next;

		EntityToDelete->UpdateGameEntity(GameStateType_Release, Entity->State);

		PlatformAPI->DeallocateMemory(EntityToDelete->State);
			
		FreePoolType(&GameState->EntitiesPool,
					 EntityToDelete);
	}

	LeaveTicketMutex(&GameState->EntityMutex);
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
	case GameStateType_Prepare: {
		InitializeMemoryStack(PlatformState,
							  PlatformAPI,
							  &State->FrameStack,
							  "FrameStack",
							  0, 1024);
		
		// NOTE(ivan): Load configuration.
		State->Config = LoadConfiguration(PlatformState, PlatformAPI, "default.cfg", 0);
		State->Config = LoadConfiguration(PlatformState, PlatformAPI, "user.cfg", &State->Config);

		// NOTE(ivan): Prepare entity system.
		InitializeMemoryPool(PlatformState,
							 PlatformAPI,
							 &State->EntitiesPool,
							 "EntitiesPool",
							 sizeof(game_entity),
							 256,
							 0);
		InitializeMemoryPool(PlatformState,
							 PlatformAPI,
							 &State->EntitiesDescPool,
							 "EntitiesDescPool",
							 sizeof(game_entity_desc),
							 64,
							 0);
	} break;

		///////////////////////////////////////////////////////////////////////////////////////////////////
		// NOTE(ivan): Game frame.
		///////////////////////////////////////////////////////////////////////////////////////////////////
	case GameStateType_Frame: {
		// NOTE(ivan): Clear surface buffer.
        DrawSolidColor(SurfaceBuffer, MakeRGBA(0.0f, 0.0f, 0.0f, 1.0f));

		// NOTE(ivan): Update game entities.
		for (game_entity *Entity = State->Entities; Entity; Entity = Entity->Next)
			Entity->UpdateGameEntity(GameStateType_Frame, Entity->State);
		
		// NOTE(ivan): Free per-frame memory arena.
		FreeMemoryStack(PlatformAPI, &State->FrameStack);
	} break;

		///////////////////////////////////////////////////////////////////////////////////////////////////
		// NOTE(ivan): Game release.
		///////////////////////////////////////////////////////////////////////////////////////////////////
	case GameStateType_Release: {
		// NOTE(ivan): Release entity system.
		KillAllEntities(PlatformAPI,
						State);
		FreeMemoryPool(PlatformAPI,
					   &State->EntitiesPool);
		FreeMemoryPool(PlatformAPI,
					   &State->EntitiesDescPool);
		
		// NOTE(ivan): Save configuration.
		SaveConfiguration(PlatformState, PlatformAPI, "user.cfg", &State->Config);
		FreeConfiguration(PlatformAPI, &State->Config);
	} break;
	}
}
