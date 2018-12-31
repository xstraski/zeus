#ifndef ZEUS_PLATFORM_H
#define ZEUS_PLATFORM_H

// NOTE(ivan): Compiler detection.
#if defined(_MSC_VER)
#define MSVC 1
#define GNUC 0
#elif defined(__GNUC__)
#define MSVC 0
#define GNUC 1
#else
#error "Unsupported compiler!"
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#if MSVC
#include <intrin.h>
#elif GNUC
#include <x86intrin.h>
#endif

// NOTE(ivan): Base integer types.
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

// NOTE(ivan): Integers large enough to hold any pointer on target processor platform.
typedef size_t uptr;
typedef ptrdiff_t sptr;

// NOTE(ivan): Float types.
typedef float f32;
typedef double f64;

typedef u32 b32; // NOTE(ivan): Forget about standard C++ bool, wa want our boolean type to be 32-bit.

// NOTE(ivan): Used to shut up the compiler complaining about an unused variable.
#define UnreferencedParam(Param) (Param)

// NOTE(ivan): These macros just makes the life easier when strings arguments needs to be collected to a buffer
#define CollectArgs(Buffer, Format) do {va_list Args; va_start(Args, Format); vsprintf(Buffer, Format, Args); va_end(Args);} while(0)
#define CollectArgsN(Buffer, BufferSize, Format) do {va_list Args; va_start(Args, Format); vsnprintf(Buffer, BufferSize, Format, Args); va_end(Args);} while(0)

// NOTE(ivan): Calculates a given array's elements count. Do not place functions/expressions as the 'arr' parameter, cuz they will be exeute twice!!!
#define CountOf(arr) (sizeof(arr) / sizeof((arr)[0]))

// NOTE(ivan): Min/max.
#define Min(A, B) ((A) <= (B) ? (A) : (B))
#define Max(A, B) ((A) > (B) ? (A) : (B))

// NOTE(ivan): Swaps data.
template <typename T> inline void
Swap(T &A, T &B) {
	T Temp = A;
	A = B;
	B = Temp;
}

// NOTE(ivan): Breaks into the debugger if any.
#define BreakDebugger() do {*((int *)0) = 1;} while(0)

// NOTE(ivan): Debug assertions.
#if SLOWCODE
#define Assert(Expression) do {if (!(Expression)) BreakDebugger();} while(0)
#else
#define Assert(Expression) do {} while(0)
#endif
#if SLOWCODE
#define Verify(Expression) Assert(Expression)
#else
#define Verify(Expression) Expression
#endif

// NOTE(ivan): Does not allow to compile the program in public release builds.
#if INTERNAL
#define NotImplemented() Assert(!"Not implemented!!!")
#else
#define NotImplemented() NotImplemented!!!!!!!!!!!!!
#endif

// NOTE(ivan): Helper macros for code pieces that are not intended to run at all.
#if INTERNAL
#define InvalidCodePath() Assert(!"Invalid code path!!!")
#else
#define InvalidCodePath() do {} while(0)
#endif
#define InvalidDefaultCase default: {InvalidCodePath();} break

// NOTE(ivan): Safe truncation.
inline u32
SafeTruncateU64(u64 Value) {
	Assert(Value <= 0xFFFFFFFF);
	return (u32)Value;
}
inline u16
SafeTruncateU32(u32 Value) {
	Assert(Value <= 0xFFFF);
	return (u16)Value;
}
inline u8
SafeTruncateU16(u16 Value) {
	Assert(Value <= 0xFF);
	return (u8)Value;
}

// NOTE(ivan): Measurings.
#define Kilobytes(val) ((val) * 1024LL)
#define Megabytes(val) (Kilobytes(val) * 1024LL)
#define Gigabytes(val) (Megabytes(val) * 1024LL)
#define Terabytes(val) (Gigabytes(val) * 1024LL)

// NOTE(ivan): Values alignment.
#define AlignPow2(val, alignment) (((val) + ((alignment) - 1)) & ~(((val) - (val)) + (alignment) - 1))
#define Align4(val) (((val) + 3) & ~3)
#define Align8(val) (((val) + 7) & ~7)
#define Align16(val) (((val) + 15) & ~15)

// NOTE(ivan): Bit scan result.
struct bit_scan_result {
	b32 IsFound;
	u32 Index;
};

// NOTE(ivan): Bit scan.
inline bit_scan_result
BitScanForward(u32 Value)
{
	bit_scan_result Result = {};
	
#if MSVC
	Result.IsFound = _BitScanForward((unsigned long *)&Result.Index, Value);
#else
	for (u32 Test = 0; Test < 32; Test++) {
		if (Value & (1 << Test)) {
			Result.IsFound = true;
			Result.Index = Test;
			break;
		}
	}
#endif	

	return Result;
}
inline bit_scan_result
BitScanReverse(u32 Value)
{
	bit_scan_result Result = {};

#if MSVC
	Result.IsFound = _BitScanReverse((unsigned long *)&Result.Index, Value);
#else
	for (u32 Test = 31; Test >= 0; Test++) {
		if (Value & (1 << Test)) {
			Result.IsFound = true;
			Result.Index = Test;
			break;
		}
	}
#endif

	return Result;
}

// NOTE(ivan): Work queue (multithreading) structure prototype, used as a handle.
struct work_queue;
// NOTE(ivan): Work queue callback function prototype.
#define WORK_QUEUE_CALLBACK(name) void name(work_queue *Queue, void *Data)
typedef WORK_QUEUE_CALLBACK(work_queue_callback);

// NOTE(ivan): Generic-purpose structure for holding a memory piece information.
struct piece {
	u8 *Memory;
	uptr Bytes;
};

// NOTE(ivan): Platform-specific state structure.
struct platform_state;

// NOTE(ivan): Platform-specific memory stats structure.
struct platform_memory_stats {
	u64 BytesTotal;
	u64 BytesAvailable;
};

// NOTE(ivan): Platform-specific interface.
#define PLATFORM_CHECK_PARAM(name) s32 name(platform_state *PlatformState, const char *Param)
typedef PLATFORM_CHECK_PARAM(platform_check_param);

#define PLATFORM_CHECK_PARAM_VALUE(name) const char * name(platform_state *PlatformState, const char *Param)
typedef PLATFORM_CHECK_PARAM_VALUE(platform_check_param_value);

#define PLATFORM_LOG(name) void name(platform_state *PlatformState, const char *MessageFormat, ...)
typedef PLATFORM_LOG(platform_log);

#define PLATFORM_ERROR(name) void name(platform_state *PlatformState, const char *ErrorFormat, ...)
typedef PLATFORM_ERROR(platform_error);

#define PLATFORM_ALLOCATE_MEMORY(name) void * name(uptr Bytes)
typedef PLATFORM_ALLOCATE_MEMORY(platform_allocate_memory);

#define PLATFORM_DEALLOCATE_MEMORY(name) void name(void *Address)
typedef PLATFORM_DEALLOCATE_MEMORY(platform_deallocate_memory);

#define PLATFORM_GET_MEMORY_STATS(name) platform_memory_stats name(void)
typedef PLATFORM_GET_MEMORY_STATS(platform_get_memory_stats);

#define PLATFORM_ADD_WORK_QUEUE_ENTRY(name) void name(work_queue *Queue, work_queue_callback *Callback, void *Data)
typedef PLATFORM_ADD_WORK_QUEUE_ENTRY(platform_add_work_queue_entry);

#define PLATFORM_COMPLETE_WORK_QUEUE(name) void name(work_queue *Queue)
typedef PLATFORM_COMPLETE_WORK_QUEUE(platform_complete_work_queue);

#define PLATFORM_READ_ENTIRE_FILE(name) piece name(const char *FileName)
typedef PLATFORM_READ_ENTIRE_FILE(platform_read_entire_file);

#define PLATFORM_FREE_ENTIRE_FILE_MEMORY(name) void name(piece *ReadResult)
typedef PLATFORM_FREE_ENTIRE_FILE_MEMORY(platform_free_entire_file_memory);

#define PLATFORM_WRITE_ENTIRE_FILE(name) b32 name(const char *FileName, void *Memory, u32 Bytes)
typedef PLATFORM_WRITE_ENTIRE_FILE(platform_write_entire_file);

// NOTE(ivan): Platform-specific interface container structure.
struct platform_api {
	platform_check_param *CheckParam;
	platform_check_param_value *CheckParamValue;
	platform_log *Log;
	platform_error *Error;
	platform_allocate_memory *AllocateMemory;
	platform_deallocate_memory *DeallocateMemory;
	platform_get_memory_stats *GetMemoryStats;
	platform_add_work_queue_entry *AddWorkQueueEntry;
	platform_complete_work_queue *CompleteWorkQueue;
	platform_read_entire_file *ReadEntireFile;
	platform_free_entire_file_memory *FreeEntireFileMemory;
	platform_write_entire_file *WriteEntireFile;

	// NOTE(ivan): Work queues.
	work_queue *HighPriorityWorkQueue;
	work_queue *LowPriorityWorkQueue;

	b32 QuitRequested; // NOTE(ivan): Set this to true to request program exit.
};

// NOTE(ivan): Helper function for reading from buffers.
#define ConsumeType(Piece, Type) (Type *)ConsumeSize(Piece, sizeof(Type))
#define ConsumeTypeArray(Piece, Type, Count) (Type *)ConsumeSize(Piece, sizeof(Type) * Count)
inline void *
ConsumeSize(piece *Piece, uptr Bytes)
{
	Assert(Piece);
	Assert(Piece->Bytes >= Bytes);
	
	void *Result = Piece->Memory;
	Piece->Memory = (Piece->Memory + Bytes);
	Piece->Bytes -= Bytes;

	return Result;
}

// NOTE(ivan): Endian-ness utilities.
inline void
SwapEndianU32(u32 *Value)
{
	Assert(Value);

#if MSVC
	*Value = _byteswap_ulong(*Value);
#else	
	u32 V = *Value;
	*Value = ((V << 24) | ((V & 0xFF00) << 8) | ((V >> 8) & 0xFF00) | (V >> 24));
#endif	
}
inline void
SwapEndianU16(u16 *Value)
{
	Assert(Value);

#if MSVC
	*Value = _byteswap_ushort(*Value);
#else
	u16 V = *Value;
	*Value = ((V << 8) || (V  >> 8));
#endif
}

// NOTE(ivan): 4-character-code.
#define FourCC(String) ((u32)((String[3] << 0) | (String[2] << 8) | (String[1] << 16) | (String[0] << 24)))
#define FastFourCC(String) (*(u32 *)(String)) // NOTE(ivan): Does not work with switch/case.

// NOTE(ivan): Memory barriers.
// TODO(ivan): _WriteBarrier()/_ReadBarrier() are "sort of" deprecated, according to MSDN:
// they say these are deprecated, but still these work fine. Should be any replacement?
#if MSVC
inline void CompletePastWritesBeforeFutureWrites(void) {_WriteBarrier(); _mm_sfence();}
inline void CompletePastReadsBeforeFutureReads(void) {_ReadBarrier(); _mm_lfence();}
#elif GNUC
inline void CompletePastWritesBeforeFutureWrites(void) {__sync_synchronize();}
inline void CompletePastReadsBeforeFutureReads(void) {__sync_synchronize();}
#else
inline void CompletePastWritesBeforeFutureWrites(void) {NotImplemented();}
inline void CompletePastReadsBeforeFutureReads(void) {NotImplemented();}
#endif

// NOTE(ivan): Interlocked operations.
#if MSVC
inline u32 AtomicIncrementU32(volatile u32 *val) {return _InterlockedIncrement((volatile long *)val);}
inline u64 AtomicIncrementU64(volatile u64 *val) {return _InterlockedIncrement64((volatile __int64 *)val);}
inline u32 AtomicDecrementU32(volatile u32 *val) {return _InterlockedDecrement((volatile long *)val);}
inline u64 AtomicDecrementU64(volatile u64 *val) {return _InterlockedDecrement64((volatile __int64 *)val);}
inline u32 AtomicCompareExchangeU32(volatile u32 *val, u32 _new, u32 expected) {return _InterlockedCompareExchange((volatile long *)val, _new, expected);}
inline u64 AtomicCompareExchangeU64(volatile u64 *val, u64 _new, u64 expected) {return _InterlockedCompareExchange64((volatile __int64 *)val, _new, expected);}
#elif GNUC
inline u32 AtomicIncrementU32(volatile u32 *val) {return __sync_fetch_and_add(val, 1);}
inline u64 AtomicIncrementU64(volatile u64 *val) {return __sync_fetch_and_add(val, 1);}
inline u32 AtomicDecrementU32(volatile u32 *val) {return __sync_fetch_and_sub(val, 1);}
inline u64 AtomicDecrementU64(volatile u64 *val) {return __sync_fetch_and_sub(val, 1);}
inline u32 AtomicCompareExchangeU32(volatile u32 *val, u32 _new, u32 expected) {return __sync_val_compare_and_swap(val, expected, _new);}
inline u64 AtomicCompareExchangeU64(volatile u64 *val, u64 _new, u64 expected) {return __sync_val_compare_and_swap(val, expected, _new);}
#else
inline u32 AtomicIncrementU32(volatile u32 *val) {NotImplemented(); return 0;}
inline u64 AtomicIncrementU64(volatile u64 *val) {NotImplemented(); return 0;}
inline u32 AtomicDecrementU32(volatile u32 *val) {NotImplemented(); return 0;}
inline u64 AtomicDecrementU64(volatile u64 *val) {NotImplemented(); return 0;}
inline u32 AtomicCompareExchangeU32(volatile u32 *val, u32 _new, u32 expected) {NotImplemented(); return 0;}
inline u64 AtomicCompareExchangeU64(volatile u64 *val, u64 _new, u64 expected) {NotImplemented(); return 0;}
#endif

// NOTE(ivan): Yield processor, give its time to other threads.
#if MSVC
inline void YieldProcessor(void) {_mm_pause();}
#elif GNUC
inline void YieldProcessor(void) {_mm_pause();}
#else
inline void YieldProcessor(void) {NotImplemented();}
#endif

// NOTE(ivan): Cross-platform ticket-mutex.
// NOTE(ivan): Any instance of this structure MUST be ZERO-initialized for proper functioning of EnterTicketMutex()/LeaveTicketMutex() macros.
struct ticket_mutex {
	volatile u64 Ticket;
	volatile u64 Serving;
};

// NOTE(ivan): Ticket-mutex locking/unlocking.
inline void
EnterTicketMutex(ticket_mutex *Mutex)
{
	Assert(Mutex);
	AtomicIncrementU64(&Mutex->Ticket);
	
	u64 Ticket = Mutex->Ticket - 1;
	while (Ticket != Mutex->Serving)
		YieldProcessor();
}
inline void
LeaveTicketMutex(ticket_mutex *Mutex)
{
	Assert(Mutex);
	AtomicIncrementU64(&Mutex->Serving);
}

#endif // #ifndef ZEUS_PLATFORM_H
