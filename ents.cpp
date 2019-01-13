#include "ents.h"

// NOTE(ivan): Player state.
struct ent_player {
	v2 Pos;
};

// NOTE(ivan): Player update.
UPDATE_GAME_ENTITY(UpdatePlayer)
{}

#if GNUC
extern "C" __attribute__((visibility("default")))
#else
extern "C"
#endif
REGISTER_ALL_ENTITIES(RegisterAllEntities)
{
	Assert(GameState);
	Assert(GameAPI);

	GameAPI->RegisterEntity(GameState, GameAPI, "Player", sizeof(ent_player), UpdatePlayer);
	//GameAPI->RegisterEntity(GameState, GameAPI, "Playe2", sizeof(ent_player), UpdatePlayer);
}
