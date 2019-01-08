#include "ents.h"

#if GNUC
extern "C" __attribute__((visibility("default")))
#else
extern "C"
#endif
REGISTER_ALL_ENTITIES(RegisterAllEntities)
{
	Assert(GameAPI);
}
