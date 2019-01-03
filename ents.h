#ifndef ENTS_H
#define ENTS_H

#include "game.h"

// NOTE(ivan): Exported to the engine, registers all its entities.
#define REGISTER_ALL_ENTITIES(name) void name(void)
typedef REGISTER_ALL_ENTITIES(register_all_entities);

#endif // #ifndef ENTS_H
