#include "game.h"
#include "game_draw.h"
#include "game_draw_group.h"

#define PushDrawGroupEntry(Group, Type) (Type *)PushDrawGroupSize(Group, sizeof(Type), DrawGroupEntryType_##Type)
static void *
PushDrawGroupSize(draw_group *Group, u32 Bytes, draw_group_entry_type Type)
{
	Assert(Group);
	Assert(Bytes);

	u8 *Result = 0;

	Bytes += sizeof(draw_group_entry_header);

	if ((Group->EntriesBytes + Bytes) < Group->EntriesMax) {
		draw_group_entry_header *Header = (draw_group_entry_header *)(Group->EntriesBase + Group->EntriesBytes);
		Header->Type = Type;

		Result = (u8 *)Header + sizeof(draw_group_entry_header);
		Group->EntriesBytes += Bytes;
	} else {
		Assert(!"Draw group entries buffer overflow!");
	}

	return Result;
}

PUSH_DRAW_GROUP_RECTANGLE(PushDrawGroupRectangle)
{
	Assert(Group);
	
	draw_group_entry_rectangle *Piece = PushDrawGroupEntry(Group, draw_group_entry_rectangle);
	Piece->Basis.Pos = Pos;
	Piece->Color = Color;
	Piece->Dim = Dim;
}

PUSH_DRAW_GROUP_IMAGE(PushDrawGroupImage)
{
	Assert(Group);
	Assert(Image);

	draw_group_entry_image *Piece = PushDrawGroupEntry(Group, draw_group_entry_image);
	Piece->Basis.Pos = Pos;
	Piece->Image = Image;
}

void
DrawGroup(draw_group *Group, game_surface_buffer *Buffer)
{
	Assert(Group);
	Assert(Buffer);

	u32 BaseAddress = 0;
	while (BaseAddress < Group->EntriesBytes) {
		draw_group_entry_header *Header = (draw_group_entry_header *)(Group->EntriesBase + BaseAddress);
		BaseAddress += sizeof(draw_group_entry_header);
		
		void *Data = (u8 *)Header + sizeof(draw_group_entry_header);
		switch(Header->Type) {
		case DrawGroupEntryType_draw_group_entry_rectangle: {
			draw_group_entry_rectangle *Entry = (draw_group_entry_rectangle *)Data;
			DrawRectangle(Buffer,
						  Entry->Basis.Pos,
						  MakeV2(Entry->Basis.Pos.X + Entry->Dim.X, Entry->Basis.Pos.Y + Entry->Dim.Y),
						  Entry->Color);
			BaseAddress += sizeof(draw_group_entry_rectangle);
		} break;

		case DrawGroupEntryType_draw_group_entry_image: {
			draw_group_entry_image *Entry = (draw_group_entry_image *)Data;
			DrawImage(Buffer,
					  Entry->Basis.Pos,
					  Entry->Image);
			BaseAddress += sizeof(draw_group_entry_image);
		} break;

			InvalidDefaultCase;
		}
	}
}
