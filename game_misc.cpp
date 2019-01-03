#include "game.h"
#include "game_misc.h"

tokenize_string_result
TokenizeString(platform_api *PlatformAPI,
			   const char *String,
			   const char *Delims)
{
	Assert(PlatformAPI);
	Assert(String);
	Assert(Delims);

	tokenize_string_result Result = {};

	const char *Ptr = String; // NOTE(ivan): Current position.
	
	// NOTE(ivan): Iterate to count tokens.
	u32 NumTokens = 0;
	b32 WasDelim = true;
	while (true) {
		if (strchr(Delims, *Ptr) || *Ptr == 0) {
			if (!WasDelim)
				NumTokens++;
			WasDelim = true;
		} else {
			WasDelim = false;
		}

		if (*Ptr == 0)
			break;

		Ptr++;
	}

	if (NumTokens == 0)
		return Result;
	Result.NumTokens = NumTokens;

	// NOTE(ivan): Allocate needed space.
	Result.Tokens = (char **)PlatformAPI->AllocateMemory(sizeof(char *) * NumTokens);
	if (!Result.Tokens)
		return Result;

	// NOTE(ivan): Iterate all over again to capture tokens.
	u32 It = 0;
	const char *Last = String;
	Ptr = String;
	WasDelim = true;
	while (true) {
		if (strchr(Delims, *Ptr) || *Ptr == 0) {
			if (!WasDelim) {
				u32 Diff = (u32)(Ptr - Last);

				Result.Tokens[It] = (char *)PlatformAPI->AllocateMemory(Diff + 1);
				if (!Result.Tokens[It]) {
					for (u32 Index = 0; Index < It - 1; Index++)
						PlatformAPI->DeallocateMemory(Result.Tokens[It]);
					PlatformAPI->DeallocateMemory(Result.Tokens);
					break;
				}

				strncpy(Result.Tokens[It], Last, Diff);
				Result.Tokens[It][Diff] = 0;

				Last = Ptr + 1;
				It++;
			}

			WasDelim = true;
		} else {
			WasDelim = false;
		}

		if (*Ptr == 0)
			break;

		Ptr++;
	}

	return Result;
}

void FreeTokenizeResult(platform_api *PlatformAPI,
						tokenize_string_result *TokenizedString)
{
	Assert(PlatformAPI);
	Assert(TokenizedString);

	for (u32 Index = 0; Index < TokenizedString->NumTokens; Index++)
		PlatformAPI->DeallocateMemory(TokenizedString->Tokens[Index]);
	PlatformAPI->DeallocateMemory(TokenizedString->Tokens);
}

u32 GetLine(const char *Source,
			u32 *SourcePos,
			char *Buffer,
			u32 BufferSize)
{
	Assert(Source);
	Assert(SourcePos);
	Assert(Buffer);
	Assert(BufferSize);

	char C;
	u32 Total = 0;
	u32 Pos = 0;
	while (true) {
		C = Source[*SourcePos + Pos];
		Pos++;

		if (C == 0)
			break; // NOTE(ivan): End of string.
		if (Total == (BufferSize - 1))
			break; // NOTE(ivan): Out of destination space.

		if (C == '\n')
			break; // NOTE(ivan): New line.

		Buffer[Total] = C;
		Total++;
	}

	// NOTE(ivan): Remove "\r" symbol if CRLF.
	if (Total) {
		if (Buffer[Total - 1] == '\r') {
			Buffer[Total] = 0;
			Total--;
		}
	}

	// NOTE(ivan): Add null terminator.
	Buffer[Total] = 0;

	*SourcePos += Pos;
	return Total;
}
