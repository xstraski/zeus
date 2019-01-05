#ifndef GAME_DRAW_GROUP_H
#define GAME_DRAW_GROUP_H

#include "game_platform.h"
#include "game_image.h"
#include "game_math.h"
#include "game_memory.h"

struct draw_basis {
	v2 Pos;
};

enum draw_group_entry_type {
	DrawGroupEntryType_draw_group_entry_rectangle,
	DrawGroupEntryType_draw_group_entry_image
};

struct draw_group_entry_header {
	draw_group_entry_type Type;
};

struct draw_group_entry_rectangle {
	draw_basis Basis;
	rgba Color;
	v2 Dim;
};

struct draw_group_entry_image {
	draw_basis Basis;
	image *Image;
	v2 Dim;
};

struct draw_group {
	u8 *EntriesBase;
	u32 EntriesBytes;
	u32 EntriesMax;

	draw_basis *DefaultBasis;
};

#define PUSH_DRAW_GROUP_RECTANGLE(name) void name(draw_group *Group, v2 Pos, v2 Dim, rgba Color)
typedef PUSH_DRAW_GROUP_RECTANGLE(push_draw_group_rectangle);

#define PUSH_DRAW_GROUP_IMAGE(name) void name(draw_group *Group, v2 Pos, image *Image)
typedef PUSH_DRAW_GROUP_IMAGE(push_draw_group_image);

PUSH_DRAW_GROUP_RECTANGLE(PushDrawGroupRectangle);
PUSH_DRAW_GROUP_IMAGE(PushDrawGroupImage);

void DrawGroup(draw_group *Group, struct game_surface_buffer *Buffer);

#endif // #ifndef GAME_DRAW_GROUP_H
