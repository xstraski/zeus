#ifndef ZEUS_MEMORY_H
#define ZEUS_MEMORY_H

#include "zeus_platform.h"

//////////////////////////////////////////////////////////////////////////////////////////////////
// NOTE(ivan): Memory stack.
//////////////////////////////////////////////////////////////////////////////////////////////////

// NOTE(ivan): Memory stack block structure.
struct memory_stack_block {
	void *Base;

	uptr BytesTotal;
	uptr BytesUsed;

	memory_stack_block *Next;
	memory_stack_block *Prev;
};

// NOTE(ivan): Memory stack structure.
// NOTE(ivan): Must be ZEROED for proper functioning.
struct memory_stack {
	const char *DebugName;
	ticket_mutex StackMutex;

	memory_stack_block *CurrentBlock;
	uptr MinBlockBytes;
};

void InitializeMemoryStack(platform_state *PlatformState,
						   platform_api *PlatformAPI,
						   memory_stack *MemoryStack,
						   const char *DebugName,
						   uptr MinBlockBytes,
						   uptr Bytes);
void InitializeMemoryStackEmpty(memory_stack *MemoryStack,
								const char *DebugName,
								uptr MinBlockBytes);
void FreeMemoryStack(platform_api *PlatformAPI,
					 memory_stack *MemoryStack);

#define PushStackType(PlatformState, PlatformAPI, MemoryStack, Type) (Type *)PushStackSize(PlatformState, PlatformAPI, MemoryStack, sizeof(Type))
#define PushStackTypeArray(PlatformState, PlatformAPI, MemoryStack, Type, Count) (Type *)PushStackSize(PlatformState, PlatformAPI, MemoryStack, sizeof(Type) * Count)
void * PushStackSize(platform_state *PlatformState,
					 platform_api *PlatformAPI,
					 memory_stack *MemoryStack,
					 uptr Bytes);

//////////////////////////////////////////////////////////////////////////////////////////////////
// NOTE(ivan): Memory pool.
//////////////////////////////////////////////////////////////////////////////////////////////////

// NOTE(ivan): Memory pool block.
struct memory_pool_block {
	struct memory_pool_chunk *Chunk;

	memory_pool_block *Next;
	memory_pool_block *Prev;
};

// NOTE(ivan): Memory pool chunk.
struct memory_pool_chunk {
	u32 NumBlocks;
	void *Blocks;
	memory_pool_block *FreeBlocks; // NOTE(ivan): Head pointer to free linked list.
	memory_pool_block *AllocBlocks; // NOTE(ivan): Head pointer to allocated linked list.

	memory_pool_chunk *Next;
};

// NOTE(ivan): Memory pool.
// NOTE(ivan): Must be ZEROED for proper functioning.
struct memory_pool {
	const char *DebugName;
	ticket_mutex PoolMutex;
	u32 DefaultMultiplier;
	
	memory_pool_chunk *Chunks;
	u32 BlockSize;
};

void InitializeMemoryPool(platform_state *PlatformState,
						  platform_api *PlatformAPI,
						  memory_pool *MemoryPool,
						  const char *DebugName,
						  u32 BlockSize,
						  u32 InitialBlocksCount,
						  u32 DefaultMultiplier);
void FreeMemoryPool(platform_api *PlatformAPI,
					memory_pool *MemoryPool);

#define PushPoolType(MemoryPool, Type) (Type *)PushPoolSize(MemoryPool, sizeof(Type))
#define PushPoolTypeArray(MemoryPool, Type, Count) (Type *)PushPoolSize(MemoryPool, sizeof(Type) * Count)
void * PushPoolSize(memory_pool *MemoryPool,
					uptr Bytes);

#define FreePoolType(PlatformState, PlatformAPI, MemoryPool, Address) FreePoolSize(PlatformState, PlatformAPI, MemoryPool, Address)
#define FreePoolTypeArray(PlatformState, PlatformAPI, MemoryPool, Address) FreePoolSize(PlatformState, PlatformAPI, MemoryPool, Address)
void FreePoolSize(platform_state *PlatformState,
				  platform_api *PlatformAPI,
				  memory_pool *MemoryPool,
				  void *Address);

#endif // #ifndef ZEUS_MEMORY_H
