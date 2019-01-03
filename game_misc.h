#ifndef ZEUS_GAME_H
#define ZEUS_GAME_H

#include "game_platform.h"

// NOTE(ivan): String tokenizer result.
struct tokenize_string_result {
	char **Tokens;
	u32 NumTokens;
};

tokenize_string_result TokenizeString(platform_api *PlatformAPI,
									  const char *String,
									  const char *Delims);
void FreeTokenizeResult(platform_api *PlatformAPI,
						 tokenize_string_result *TokenizedString);

u32 GetLine(const char *Source,
			u32 *SourcePos,
			char *Buffer,
			u32 BufferSize);

#endif // #ifndef GAME_MISC_H
