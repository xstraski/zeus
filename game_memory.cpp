#include "game.h"

//////////////////////////////////////////////////////////////////////////////////////////////////
// NOTE(ivan): Memory stack.
//////////////////////////////////////////////////////////////////////////////////////////////////

static memory_stack_block *
AllocateMemoryStackBlock(platform_api *PlatformAPI,
						 uptr Bytes,
						 memory_stack_block *PrevBlock)
{
	Assert(PlatformAPI);
	Assert(Bytes);
	
	memory_stack_block *Result = (memory_stack_block *)PlatformAPI->AllocateMemory(sizeof(memory_stack_block));
	if (Result) {
		Result->Base = PlatformAPI->AllocateMemory(Bytes);
		if (Result->Base) {
			Result->BytesTotal = Bytes;
			Result->BytesUsed = 0;
			Result->Next = 0;
			Result->Prev = PrevBlock;

			if (PrevBlock)
				PrevBlock->Next = Result;
		} else {
			PlatformAPI->DeallocateMemory(Result);
		}
	}

	return Result;
}

void
InitializeMemoryStack(platform_state *PlatformState,
					  platform_api *PlatformAPI,
					  memory_stack *MemoryStack,
					  const char *DebugName,
					  uptr MinBlockBytes,
					  uptr Bytes)
{
	Assert(PlatformState);
	Assert(PlatformAPI);
	Assert(MemoryStack);
	Assert(DebugName);
	Assert(Bytes);

	EnterTicketMutex(&MemoryStack->StackMutex);
	
	MemoryStack->DebugName = DebugName;	
	MemoryStack->MinBlockBytes = MinBlockBytes ? MinBlockBytes : 1024; // TODO(ivan): Tune default minimal block size eventually.

	MemoryStack->CurrentBlock = AllocateMemoryStackBlock(PlatformAPI, Bytes, 0);
	if (!MemoryStack->CurrentBlock)  {
		LeaveTicketMutex(&MemoryStack->StackMutex);
		PlatformAPI->Log(PlatformState, "MemoryStack[%s]: Out of memory!", MemoryStack->DebugName);
		return;
	}

	LeaveTicketMutex(&MemoryStack->StackMutex);
}

void
InitializeMemoryStackEmpty(memory_stack *MemoryStack,
						   const char *DebugName,
						   uptr MinBlockBytes)
{
	Assert(MemoryStack);
	Assert(DebugName);

	EnterTicketMutex(&MemoryStack->StackMutex);

	MemoryStack->DebugName = DebugName;
	MemoryStack->MinBlockBytes = MinBlockBytes ? MinBlockBytes : 1024; // NOTE(ivan): Tune default minimal block size eventually.

	MemoryStack->CurrentBlock = 0;

	LeaveTicketMutex(&MemoryStack->StackMutex);
}

void
FreeMemoryStack(platform_api *PlatformAPI,
				memory_stack *MemoryStack)
{
	Assert(PlatformAPI);
	Assert(MemoryStack);

	EnterTicketMutex(&MemoryStack->StackMutex);

	if (!MemoryStack->CurrentBlock) {
		LeaveTicketMutex(&MemoryStack->StackMutex);
		return;
	}

	memory_stack_block *Block = MemoryStack->CurrentBlock;
	while (Block) {
		memory_stack_block *BlockToDelete = Block;
		Block = Block->Next;
		PlatformAPI->DeallocateMemory(BlockToDelete->Base);
		PlatformAPI->DeallocateMemory(BlockToDelete);
	}

	MemoryStack->CurrentBlock = 0;

	LeaveTicketMutex(&MemoryStack->StackMutex);
}

void *
PushStackSize(platform_state *PlatformState,
			  platform_api *PlatformAPI,
			  memory_stack *MemoryStack, uptr Bytes)
{
	Assert(PlatformState);
	Assert(PlatformAPI);
	Assert(MemoryStack);
	Assert(Bytes);

	EnterTicketMutex(&MemoryStack->StackMutex);

	memory_stack_block *TargetBlock = 0;
	if (MemoryStack->CurrentBlock) {
		TargetBlock = MemoryStack->CurrentBlock;
		while (TargetBlock) {
			if (TargetBlock->BytesTotal - TargetBlock->BytesUsed >= Bytes)
				break;
		}
	}
	if (!TargetBlock) {
		memory_stack_block *NewBlock = AllocateMemoryStackBlock(PlatformAPI,
																Max(MemoryStack->MinBlockBytes, Bytes),
																TargetBlock);
		if (!NewBlock) {
			LeaveTicketMutex(&MemoryStack->StackMutex);
			PlatformAPI->Log(PlatformState, "MemoryStack[%s]: Out of memory!", MemoryStack->DebugName);
			return 0;
		}
		MemoryStack->CurrentBlock = NewBlock;
		TargetBlock = NewBlock;
	}

	void *Result = (void *)((uptr)TargetBlock->Base + TargetBlock->BytesUsed);
	TargetBlock->BytesUsed += Bytes;

	LeaveTicketMutex(&MemoryStack->StackMutex);

	return Result;
}

//////////////////////////////////////////////////////////////////////////////////////////////////
// NOTE(ivan): Memory pool.
//////////////////////////////////////////////////////////////////////////////////////////////////

static memory_pool_chunk *
AllocateMemoryPoolChunk(platform_api *PlatformAPI,
						u32 BlockSize, u32 NumBlocks)
{
	Assert(PlatformAPI);
	Assert(BlockSize);
	Assert(NumBlocks);

	memory_pool_chunk *Chunk = (memory_pool_chunk *)PlatformAPI->AllocateMemory(sizeof(memory_pool_chunk));
	if (!Chunk)
		return 0;

	Chunk->NumBlocks = NumBlocks;
	Chunk->Blocks = PlatformAPI->AllocateMemory(NumBlocks * (sizeof(memory_pool_block) + BlockSize));
	Chunk->FreeBlocks = 0;
	Chunk->AllocBlocks = 0;
	Chunk->Next = 0;

	for (u32 Index = 0; Index < NumBlocks; Index++) {
		memory_pool_block *Block = (memory_pool_block *)((u8 *)Chunk->Blocks + Index * (BlockSize + sizeof(memory_pool_block)));
		if (!Block) {
			PlatformAPI->DeallocateMemory(Chunk->Blocks);
			return 0;
		}

		Block->Chunk = Chunk;
		Block->Prev = 0;
		Block->Next = Chunk->FreeBlocks;
		if (Chunk->FreeBlocks)
			Chunk->FreeBlocks->Prev = Block;
		Chunk->FreeBlocks = Block;
	}

	return Chunk;
}

static void
FreeMemoryPoolChunk(platform_api *PlatformAPI,
					memory_pool_chunk *Chunk)
{
	Assert(PlatformAPI);
	Assert(Chunk);

	PlatformAPI->DeallocateMemory(Chunk->Blocks);
	PlatformAPI->DeallocateMemory(Chunk);
}

void
InitializeMemoryPool(platform_state *PlatformState,
					 platform_api *PlatformAPI,
					 memory_pool *MemoryPool,
					 const char *DebugName,
					 u32 BlockSize,
					 u32 InitialBlocksCount,
					 u32 DefaultMultiplier)
{
	Assert(PlatformState);
	Assert(PlatformAPI);
	Assert(MemoryPool);
	Assert(DebugName);
	Assert(BlockSize);
	Assert(InitialBlocksCount);
	Assert(DefaultMultiplier == 0 || DefaultMultiplier > 1);

	EnterTicketMutex(&MemoryPool->PoolMutex);

	MemoryPool->DebugName = DebugName;
	MemoryPool->BlockSize = BlockSize;
	MemoryPool->DefaultMultiplier = DefaultMultiplier ? DefaultMultiplier : 2; // TODO(ivan): Tune default multiplier value eventually.

	// NOTE(ivan): Allocate first chunk.
	MemoryPool->Chunks = AllocateMemoryPoolChunk(PlatformAPI, BlockSize, InitialBlocksCount);
	if (!MemoryPool->Chunks) {
		LeaveTicketMutex(&MemoryPool->PoolMutex);
		PlatformAPI->Log(PlatformState, "MemoryPool[%s]: Out of memory!", DebugName);
		return;
	}

	LeaveTicketMutex(&MemoryPool->PoolMutex);
}

void
FreeMemoryPool(platform_api *PlatformAPI,
			   memory_pool *MemoryPool)
{
	Assert(MemoryPool);

	EnterTicketMutex(&MemoryPool->PoolMutex);

	if (!MemoryPool->Chunks) {
		LeaveTicketMutex(&MemoryPool->PoolMutex);
		return;
	}

	memory_pool_chunk *Chunk = MemoryPool->Chunks;
	while (Chunk) {
		memory_pool_chunk *ChunkToDelete = Chunk;
		Chunk = Chunk->Next;

		FreeMemoryPoolChunk(PlatformAPI, ChunkToDelete);
	}

	LeaveTicketMutex(&MemoryPool->PoolMutex);
}

void *
PushPoolSize(platform_state *PlatformState,
			 platform_api *PlatformAPI,
			 memory_pool *MemoryPool)
{
	Assert(PlatformState);
	Assert(PlatformAPI);
	Assert(MemoryPool);

	EnterTicketMutex(&MemoryPool->PoolMutex);

	// NOTE(ivan): Search for a chunk that is not full.
	memory_pool_chunk *Chunk = MemoryPool->Chunks;
	while (!Chunk->FreeBlocks) {
		Chunk = Chunk->Next;
		if (!Chunk)
			break;
	}

	// NOTE(ivan): All chunks are full?
	if (!Chunk) {
		// NOTE(ivan): Allocate new chunk.
		memory_pool_chunk *NewChunk = AllocateMemoryPoolChunk(PlatformAPI,
															  MemoryPool->BlockSize,
															  MemoryPool->Chunks->NumBlocks * MemoryPool->DefaultMultiplier); // NOTE(ivan): Tune allocation multiplier eventually.
		if (!NewChunk) {
			LeaveTicketMutex(&MemoryPool->PoolMutex);
			PlatformAPI->Log(PlatformState, "MemoryPool[%s]: Out of memory!", MemoryPool->DebugName);
			return 0;
		}
		
		NewChunk->Next = MemoryPool->Chunks;
		MemoryPool->Chunks = NewChunk;
		Chunk = NewChunk;
	}

	// NOTE(ivan): Allocate space.
	memory_pool_block *Block = Chunk->FreeBlocks;
	Chunk->FreeBlocks = Block->Next;
	if (Chunk->FreeBlocks)
		Chunk->FreeBlocks->Prev = 0;

	Block->Next = Chunk->AllocBlocks;
	if (Chunk->AllocBlocks)
		Chunk->AllocBlocks->Prev = Block;
	Chunk->AllocBlocks = Block;

	LeaveTicketMutex(&MemoryPool->PoolMutex);

	return (void *)((u8 *)Block + sizeof(memory_pool_block));
}

void
FreePoolSize(memory_pool *MemoryPool, void *Address)
{
	Assert(MemoryPool);

	if (!Address)
		return;

	EnterTicketMutex(&MemoryPool->PoolMutex);

	memory_pool_block *Block = (memory_pool_block *)((u8 *)Address - sizeof(memory_pool_block));
	memory_pool_chunk *Chunk = Block->Chunk;

	Chunk->AllocBlocks = Block->Next;
	if (Chunk->AllocBlocks)
		Chunk->AllocBlocks->Prev = 0;

	Block->Next = Chunk->FreeBlocks;
	if (Block->Next)
		Block->Next->Prev = Block;

	Chunk->FreeBlocks = Block;

	LeaveTicketMutex(&MemoryPool->PoolMutex);
}
