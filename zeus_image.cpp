#include "zeus.h"
#include "zeus_image.h"

// NOTE(ivan): Bitmap file header structure.
#pragma pack(push, 1)
struct bmp_header {
	u16 FileType;
	u32 FileSize;
	u16 Reserved1;
	u16 Reserved2;
	u32 BitmapOffset;
	u32 Size;
	s32 Width;
	s32 Height;
	u16 Planes;
	u16 BitsPerPixel;
	u32 Compression;
	u32 SizeOfBitmap;
	s32 HorzResolution;
	s32 VertResolution;
	u32 ColorsUsed;
	u32 ColorsImportant;
	u32 RedMask;
	u32 GreenMask;
	u32 BlueMask;
};
#pragma pack(pop)

image
LoadBMP(platform_state *PlatformState,
		platform_api *PlatformAPI,
		const char *FileName)
{
	// NOTE(ivan): Remember, that this is NOT a complete BMP loading code,
	// it supports only 32-bit XRGB bitmaps with no compression and negative height!
	
	Assert(PlatformState);
	Assert(PlatformAPI);
	Assert(FileName);

	image Result = {};

	PlatformAPI->Log(PlatformState, "Loading BMP file '%s'...", FileName);
	
	piece ReadPiece = PlatformAPI->ReadEntireFile(FileName);
	if (ReadPiece.Memory) {
		bmp_header *Header = (bmp_header *)ReadPiece.Memory;

		static const u8 BMPSignature[] = {0x42, 0x4D};
		if (Header->FileType != *(u32 *)&BMPSignature)
			PlatformAPI->Log(PlatformState, "BMP file '%s' has wrong signature!", FileName);

		if (Header->BitsPerPixel != 32) {
			PlatformAPI->Log(PlatformState, "BMP file '%s' is not 32-bit, not supported.", FileName);
		} else {
			u32 *Pixels = (u32 *)((u8 *)ReadPiece.Memory + Header->BitmapOffset);
			
			bit_scan_result RedShift = BitScanForward(Header->RedMask);
			bit_scan_result GreenShift = BitScanForward(Header->GreenMask);
			bit_scan_result BlueShift = BitScanForward(Header->BlueMask);
			bit_scan_result AlphaShift = BitScanForward(Header->RedMask | Header->GreenMask | Header->BlueMask);
			
			Assert(RedShift.IsFound);
			Assert(GreenShift.IsFound);
			Assert(BlueShift.IsFound);
			Assert(AlphaShift.IsFound);
			
			u32 *SourceDest = Pixels;
			for (s32 Y = 0; Y < Header->Height; Y++) {
				for (s32 X = 0; X < Header->Width; X++) {
					u32 C = *SourceDest;
					*SourceDest++ = ((((C >> AlphaShift.Index) & 0xFF) << 24) |
									 (((C >> RedShift.Index) & 0xFF) << 16) |
									 (((C >> GreenShift.Index) & 0xFF) << 8) |
									 (((C >> BlueShift.Index) & 0xFF) << 0));
				}
			}
				
			Result.Pixels = PlatformAPI->AllocateMemory(Header->Width * Header->Height * Header->BitsPerPixel / 8);
			if (Result.Pixels) {
				memcpy(Result.Pixels, Pixels, Header->Width * Header->Height * Header->BitsPerPixel / 8);
					
				Result.Width = Header->Width;
				Result.Height = Header->Height;
				Result.BytesPerPixel = Header->BitsPerPixel / 8;
				Result.Pitch = Header->Width * Header->BitsPerPixel / 8;
			} else {
				PlatformAPI->Log(PlatformState, "BMP file '%s' is too large.", FileName);
			}
		}
		
		PlatformAPI->FreeEntireFileMemory(&ReadPiece);
	} else {
		PlatformAPI->Log(PlatformState, "BMP file '%s' not found!", FileName);
	}

	return Result;
}

void
FreeImage(platform_api *PlatformAPI,
		  image *Image)
{
	Assert(PlatformAPI);
	Assert(Image);
	
	PlatformAPI->DeallocateMemory(Image->Pixels);
}
