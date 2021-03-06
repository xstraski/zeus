#ifndef GAME_IMAGE_H
#define GAME_IMAGE_H

#include "game_platform.h"

// NOTE(ivan): Image structure.
struct image {
	void *Pixels; // NOTE(ivan): Format - 0xAARRGGBB.

	s32 Width;
	s32 Height;
	s32 BytesPerPixel;
	s32 Pitch;
};

image LoadBMP(platform_state *PlatformState,
			  platform_api *PlatformAPI,
			  const char *FileName);

void FreeImage(platform_api *PlatformAPI,
			   image *Image);

#endif // #ifndef GAME_IMAGE_H
