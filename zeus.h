#ifndef ZEUS_H
#define ZEUS_H

#include "zeus_platform.h"
#include "zeus_memory.h"
#include "zeus_keys.h"
#include "zeus_image.h"

// NOTE(ivan): Title.
#define GAMENAME "ZeusDemo"

// NOTE(ivan): Version.
#define GAMEVER_MAJOR 1
#define GAMEVER_MINOR 0
#define GAMEVER_PATCH 0

// NOTE(ivan): Timings.
struct game_clocks {
	f64 SecondsPerFrame;
	u32 FramesPerSecond;
};

// NOTE(ivan): Off-screen graphics buffer structure.
struct game_surface_buffer {
	void *Pixels; // NOTE(ivan): Format - 0xAARRGGBB.
	s32 Width;
	s32 Height;
	s32 BytesPerPixel;
	s32 Pitch;
};

// NOTE(ivan): Input button state.
struct game_input_button {
	b32 IsActual; // NOTE(ivan): Is data actual for current frame?
	b32 IsDown;
	b32 WasDown;
	u32 HalfTransitionCount; // NOTE(ivan): number of half-transitions (one down or one up event) per frame.
};

inline b32
IsSinglePress(game_input_button Button)
{
	return Button.IsDown && !Button.WasDown && Button.IsActual;
}

// NOTE(ivan): Input controller.
struct game_input_controller {
	b32 IsConnected;

	game_input_button Start;
	game_input_button Back;
	
	game_input_button A;
	game_input_button B;
	game_input_button X;
	game_input_button Y;

	game_input_button Up;
	game_input_button Down;
	game_input_button Left;
	game_input_button Right;

	game_input_button LeftBumper;
	game_input_button RightBumper;

	game_input_button LeftStick;
	game_input_button RightStick;

	// NOTE(ivan): These are analog.
	u8 LeftTrigger;
	u8 RightTrigger;

	// NOTE(ivan): These are analog.
	f32 LeftStickX;
	f32 LeftStickY;
	f32 RightStickX;
	f32 RightStickY;
};

// NOTE(ivan): Input state.
// TODO(ivan): Mousewheel?
struct game_input {
	u32 MouseX;
	u32 MouseY;
	game_input_button MouseButtons[5]; // NOTE(ivan): Left, middle, right, X1, and X2.

	game_input_button KeyboardButtons[KeyCode_MaxCount];
	game_input_controller Controllers[4];
};

// NOTE(ivan): Game configuration entry.
struct game_config_entry {
	char Name[256];
	char Value[64];

	game_config_entry *Next;
	game_config_entry *Prev;
};

// NOTE(ivan): Game configuration cache.
// NOTE(ivan): This structure MUST be ZEROED for proper functioning.
struct game_config {
	memory_stack ConfigStack; // TOOD(ivan): MAYBE pool instead of stack?
	game_config_entry *LastEntry;
	ticket_mutex ConfigMutex;
};

// NOTE(ivan): Game state type.
enum game_state_type {
    GameStateType_Init,
	GameStateType_Frame,
	GameStateType_Shutdown
};

// NOTE(ivan): Game state.
// NOTE(ivan): Game state structure allocated in platform layer MUST be ZEROED.
struct game_state {
	game_state_type Type;
	game_config Config;

	memory_stack FrameStack;

	image TestImage;
};

// NOTE(ivan): This is program's main body, gets called each frame.
void UpdateGame(platform_state *PlatformState,
				platform_api *PlatformAPI,
				game_clocks *Clocks,
				game_surface_buffer *SurfaceBuffer,
				game_input *Input,
				game_state *State);

// NOTE(ivan): Configuration cache operations.
game_config LoadConfiguration(platform_state *PlatformState,
							  platform_api *PlatformAPI,
							  const char *FileName,
							  game_config *Config);
void SaveConfiguration(platform_state *PlatformState,
					   platform_api *PlatformAPI,
					   const char *FileName,
					   game_config *Config);
void FreeConfiguration(platform_api *PlatformAPI,
					   game_config *Config);
inline const char *
GetConfigurationValue(game_config *Config,
					  const char *Name,
					  const char *Default)
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

#endif // #ifndef ZEUS_H
