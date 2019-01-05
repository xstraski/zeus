#ifndef GAME_DRAW_H
#define GAME_DRAW_H

#include "game_platform.h"
#include "game_math.h"
#include "game_image.h"

void DrawPixel(game_surface_buffer *Buffer,
			   v2 Pos,
			   rgba Color);

void DrawRectangle(game_surface_buffer *Buffer,
				   v2 Pos0,
				   v2 Pos1,
				   rgba Color);

void DrawImage(game_surface_buffer *Buffer,
			   v2 Pos,
			   image *Image);

#endif // #ifndef GAME_DRAW_H
