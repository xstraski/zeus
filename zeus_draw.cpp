#include "zeus.h"
#include "zeus_draw.h"

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
    
	s32 PosX = (u32)roundf(Buffer->Width * Pos.X);
	s32 PosY = (u32)roundf(Buffer->Height * Pos.Y);
    
	u8 *Row = ((u8 *)Buffer->Pixels + (PosY * Buffer->Pitch));
	u32 *Pixel = (u32 *)(Row + (PosX * Buffer->BytesPerPixel));
    *Pixel = Color32;
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

	s32 PosX0 = (u32)roundf(Buffer->Width * Pos0.X);
	s32 PosX1 = (u32)roundf(Buffer->Width * Pos1.X);
	s32 PosY0 = (u32)roundf(Buffer->Height * Pos0.Y);
	s32 PosY1 = (u32)roundf(Buffer->Height * Pos1.Y);

	u8 *Row = ((u8 *)Buffer->Pixels + (PosY0 * Buffer->Pitch));
	for (s32 Y = PosY0; Y < PosY1; Y++) {
		u32 *Pixel = (u32 *)(Row + (PosX0 * Buffer->BytesPerPixel));
		for (s32 X = PosX0; X < PosX1; X++)
			*Pixel++ = Color32;

		Row += Buffer->Pitch;
	}
}

void
DrawImage(game_surface_buffer *Buffer,
		  v2 Pos,
		  image *Image)
{
	Assert(Buffer);
	Assert(Image);

	s32 PosX0 = (u32)roundf(Buffer->Width * Pos.X);
	s32 PosY0 = (u32)roundf(Buffer->Height * Pos.Y);
	s32 PosX1 = PosX0 + Image->Width;
	s32 PosY1 = PosY0 + Image->Height;

	u8 *DestRow = ((u8 *)Buffer->Pixels + (PosY0 * Buffer->Pitch));
#if 1	
	u8 *SourceRow = (u8 *)Image->Pixels + Image->Pitch * (Image->Height - 1);
#else	
	u8 *SourceRow = (u8 *)Image->Pixels;
#endif	
	for (s32 Y = PosY0; Y < PosY1; Y++) {
		u32 *DestPixel = (u32 *)(DestRow + (PosX0 * Buffer->BytesPerPixel));
		u32 *SourcePixel = (u32 *)SourceRow;
		for (s32 X = PosX0; X < PosX1; X++) {
			u32 SourceC = *SourcePixel;
			u32 DestC = *DestPixel;

			blending_result BResult = DoLinearBlending(((SourceC >> 24) & 0xFF) / 255.0f,
													   (u8)((SourceC >> 16) & 0xFF),
													   (u8)((SourceC >> 8) & 0xFF),
													   (u8)((SourceC >> 0) & 0xFF),
													   (u8)((DestC >> 16) & 0xFF),
													   (u8)((DestC >> 8) & 0xFF),
													   (u8)((DestC >> 0) & 0xFF));
			*DestPixel = (u32)((BResult.R << 16) |
							   (BResult.G << 8) |
							   (BResult.B << 0));

			SourcePixel++;
			DestPixel++;
		}
			
		DestRow += Buffer->Pitch;
#if 1		
		SourceRow -= Image->Pitch;
#else
		SourceRow += Image->Pitch;
#endif		
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

void
DrawWeirdGradient(game_surface_buffer *Buffer,
				  u32 XOffset,
				  u32 YOffset)
{
	Assert(Buffer);
	
	u8 *Row = (u8 *)Buffer->Pixels;
	for (s32 Y = 0; Y < Buffer->Height; Y++) {
		u32 *Pixel = (u32 *)Row;
		for (s32 X = 0; X < Buffer->Width; X++) {
			*Pixel++ = (((u8)(X + XOffset) << 8) | (u8)(Y + YOffset));
		}
		
		Row += Buffer->Pitch;
	}
}
