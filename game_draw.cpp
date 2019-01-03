#include "game.h"
#include "game_draw.h"

// NOTE(ivan): Alpha blendin result structure.
struct blending_result {
	u8 R;
	u8 G;
	u8 B;
};

inline blending_result
DoLinearBlending(f32 AlphaChannel,
				 u8 DestR, u8 DestG, u8 DestB,
				 u8 SourceR, u8 SourceG, u8 SourceB)
{
	blending_result Result;

	Result.R = (u8)roundf((1.0f - AlphaChannel) * DestR + AlphaChannel * SourceR);
	Result.G = (u8)roundf((1.0f - AlphaChannel) * DestG + AlphaChannel * SourceG);
	Result.B = (u8)roundf((1.0f - AlphaChannel) * DestB + AlphaChannel * SourceB);

	return Result;
}

void
DrawPixel(game_surface_buffer *Buffer,
		  v2 Pos,
		  rgba Color)
{
	Assert(Buffer);
    
	u8 ColorR = (u8)roundf(Color.R * 255.0f);
	u8 ColorG = (u8)roundf(Color.G * 255.0f);
	u8 ColorB = (u8)roundf(Color.B * 255.0f);
	u8 ColorA = (u8)roundf(Color.A * 255.0f);
	u32 Color32 = (((u8)ColorA << 24) |
				   ((u8)ColorR << 16) |
				   ((u8)ColorG << 8) |
				   ((u8)ColorB));
    
	s32 PosX = (u32)roundf(Pos.X);
	s32 PosY = (u32)roundf(Pos.Y);

	if (PosX < 0)
		return;
	if (PosY < 0)
		return;
	if (PosX >= Buffer->Width)
		return;
	if (PosY >= Buffer->Height)
		return;
    
	u8 *Row = ((u8 *)Buffer->Pixels + (PosY * Buffer->Pitch));
	u32 *Pixel = (u32 *)(Row + (PosX * Buffer->BytesPerPixel));

	u32 SourceC = Color32;
	u32 DestC = *Pixel;

	// TODO(ivan): Premultiplied alpha!!!
	blending_result BResult = DoLinearBlending(((SourceC >> 24) & 0xFF) / 255.0f,
											   (u8)((DestC >> 16) & 0xFF),
											   (u8)((DestC >> 8) & 0xFF),
											   (u8)((DestC >> 0) & 0xFF),
											   (u8)((SourceC >> 16) & 0xFF),
											   (u8)((SourceC >> 8) & 0xFF),
											   (u8)((SourceC >> 0) & 0xFF));
	*Pixel = (((u32)BResult.R << 16) |
			  ((u32)BResult.G << 8) |
			  ((u32)BResult.B << 0));
}

void
DrawRectangle(game_surface_buffer *Buffer,
			  v2 Pos0,
			  v2 Pos1,
			  rgba Color)
{
	Assert(Buffer);

	u8 ColorR = (u8)roundf(Color.R * 255.0f);
	u8 ColorG = (u8)roundf(Color.G * 255.0f);
	u8 ColorB = (u8)roundf(Color.B * 255.0f);
	u8 ColorA = (u8)roundf(Color.A * 255.0f);
	u32 Color32 = (((u8)ColorA << 24) |
				   ((u8)ColorR << 16) |
				   ((u8)ColorG << 8) |
				   ((u8)ColorB));

	s32 PosX0 = (u32)roundf(Pos0.X);
	s32 PosX1 = (u32)roundf(Pos1.X);
	s32 PosY0 = (u32)roundf(Pos0.Y);
	s32 PosY1 = (u32)roundf(Pos1.Y);

	Assert(PosX1 > PosX0);
	Assert(PosY1 > PosY0);

	for (s32 Y = PosY0; Y < PosY1; Y++) {
		if (Y < 0)
			continue;
		if (Y > Buffer->Height)
			continue;

		u8 *Row = ((u8 *)Buffer->Pixels + (Y * Buffer->Pitch));
		
		for (s32 X = PosX0; X < PosX1; X++) {
			if (X < 0)
				continue;
			if (X > Buffer->Width)
				continue;
			
			u32 *Pixel = (u32 *)(Row + (X * Buffer->BytesPerPixel));
			
			u32 SourceC = Color32;
			u32 DestC = *Pixel;

			// TODO(ivan): Premultiplied alpha!!!
			blending_result BResult = DoLinearBlending(((SourceC >> 24) & 0xFF) / 255.0f,
													   (u8)((DestC >> 16) & 0xFF),
													   (u8)((DestC >> 8) & 0xFF),
													   (u8)((DestC >> 0) & 0xFF),
													   (u8)((SourceC >> 16) & 0xFF),
													   (u8)((SourceC >> 8) & 0xFF),
													   (u8)((SourceC >> 0) & 0xFF));
			*Pixel = (((u32)BResult.R << 16) |
					  ((u32)BResult.G << 8) |
					  ((u32)BResult.B << 0));
			Pixel++;
		}
	}
}

void
DrawImage(game_surface_buffer *Buffer,
		  v2 Pos,
		  image *Image)
{
	Assert(Buffer);
	Assert(Image);

	s32 PosX0 = (u32)roundf(Pos.X);
	s32 PosY0 = (u32)roundf(Pos.Y);
	s32 PosX1 = PosX0 + Image->Width;
	s32 PosY1 = PosY0 + Image->Height;

	for (s32 Y = PosY0; Y < PosY1; Y++) {
		if (Y < 0)
			continue;
		if (Y >= Buffer->Height)
			continue;
		
		u8 *DestRow = ((u8 *)Buffer->Pixels + (Y * Buffer->Pitch));
		u8 *SourceRow = (u8 *)Image->Pixels + ((Y - PosY0) * Image->Pitch);
		
		for (s32 X = PosX0; X < PosX1; X++) {
			if (X < 0)
				continue;
			if (X >= Buffer->Width)
				continue;

			u32 *DestPixel = (u32 *)(DestRow + (X * Buffer->BytesPerPixel));
			u32 *SourcePixel = (u32 *)(SourceRow + ((X - PosX0) * Image->BytesPerPixel));
			
			u32 SourceC = *SourcePixel;
			u32 DestC = *DestPixel;

			// TODO(ivan): Premultiplied alpha!!!
			blending_result BResult = DoLinearBlending(((SourceC >> 24) & 0xFF) / 255.0f,
													   (u8)((DestC >> 16) & 0xFF),
													   (u8)((DestC >> 8) & 0xFF),
													   (u8)((DestC >> 0) & 0xFF),
													   (u8)((SourceC >> 16) & 0xFF),
													   (u8)((SourceC >> 8) & 0xFF),
													   (u8)((SourceC >> 0) & 0xFF));
			*DestPixel = (((u32)BResult.R << 16) |
						  ((u32)BResult.G << 8) |
						  ((u32)BResult.B << 0));
		}
	}
}

void
DrawSolidColor(game_surface_buffer *Buffer,
			   rgba Color)
{
	Assert(Buffer);
    
	u8 ColorR = (u8)roundf(Color.R * 255.0f);
	u8 ColorG = (u8)roundf(Color.G * 255.0f);
	u8 ColorB = (u8)roundf(Color.B * 255.0f);
	u8 ColorA = (u8)roundf(Color.A * 255.0f);
	u32 Color32 = (((u8)ColorA << 24) |
				   ((u8)ColorR << 16) |
				   ((u8)ColorG << 8) |
				   ((u8)ColorB));
	
	u8 *Row = (u8 *)Buffer->Pixels;
	for (s32 Y = 0; Y < Buffer->Height; Y++) {
		u32 *Pixel = (u32 *)Row;
		for (s32 X = 0; X < Buffer->Width; X++) {
			*Pixel++ = Color32;
		}
		
		Row += Buffer->Pitch;
	}
}
