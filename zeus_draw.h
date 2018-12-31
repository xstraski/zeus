#ifndef ZEUS_DRAW_H
#define ZEUS_DRAW_H

#include "zeus_platform.h"
#include "zeus_math.h"
#include "zeus_image.h"

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

void DrawSolidColor(game_surface_buffer *Buffer,
					rgba Color);

void DrawWeirdGradient(game_surface_buffer *Buffer,
					   u32 XOffset,
					   u32 YOffset);

#endif // #ifndef ZEUS_DRAW_H
