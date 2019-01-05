#ifndef ENTS_H
#define ENTS_H

#include "game.h"

// NOTE(ivan): Game entities exported to the engine function prototoype.
#define REGISTER_ALL_ENTITIES(name) void name(game_api *GameAPI)
typedef REGISTER_ALL_ENTITIES(register_all_entities);

#endif // #ifndef ENTS_H
