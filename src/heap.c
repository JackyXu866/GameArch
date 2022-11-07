#include "heap.h"

#include "debug.h"
#include "mutex.h"
#include "tlsf/tlsf.h"

#include <stddef.h>
#include <stdio.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define CALLSTACK_S 1

typedef struct arena_t
{
	pool_t pool;
	struct arena_t* next;
} arena_t;

typedef struct heap_t
{
	tlsf_t tlsf;
	size_t grow_increment;
	arena_t* arena;
	mutex_t* mutex;
} heap_t;


static void default_walker(void* ptr, size_t size, int used, void* user);

heap_t* heap_create(size_t grow_increment)
{
	heap_t* heap = VirtualAlloc(NULL, sizeof(heap_t) + tlsf_size(),
		MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!heap)
	{
		debug_print(
			k_print_error,
			"OUT OF MEMORY!\n");
		return NULL;
	}

	heap->mutex = mutex_create();
	heap->grow_increment = grow_increment;
	heap->tlsf = tlsf_create(heap + 1);
	heap->arena = NULL;

	return heap;
}

void* heap_alloc(heap_t* heap, size_t size, size_t alignment)
{
	mutex_lock(heap->mutex);
	size = sizeof(void*)*CALLSTACK_S + size;

	void* address = tlsf_memalign(heap->tlsf, alignment, size);
	if (!address)
	{
		size_t arena_size =
			__max(heap->grow_increment, size * 2) +
			sizeof(arena_t);
		arena_t* arena = VirtualAlloc(NULL,
			arena_size + tlsf_pool_overhead(),
			MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		if (!arena)
		{
			debug_print(
				k_print_error,
				"OUT OF MEMORY!\n");
			return NULL;
		}

		arena->pool = tlsf_add_pool(heap->tlsf, arena + 1, arena_size);

		arena->next = heap->arena;
		heap->arena = arena;

		address = tlsf_memalign(heap->tlsf, alignment, size);
	}

	debug_backtrace((void**)address, CALLSTACK_S);

	mutex_unlock(heap->mutex);
	
	return (char*)address + sizeof(void*) * CALLSTACK_S;
}

void heap_free(heap_t* heap, void* address)
{
	mutex_lock(heap->mutex);
	tlsf_free(heap->tlsf, (char*)address- sizeof(void*) * CALLSTACK_S);
	mutex_unlock(heap->mutex);
}

void heap_destroy(heap_t* heap)
{
	symbol_init();
	tlsf_destroy(heap->tlsf);

	arena_t* arena = heap->arena;
	while (arena)
	{
		tlsf_walk_pool(arena->pool, default_walker, heap);
		arena_t* next = arena->next;
		VirtualFree(arena, 0, MEM_RELEASE);
		arena = next;
	}

	mutex_destroy(heap->mutex);

	VirtualFree(heap, 0, MEM_RELEASE);

	symbol_clean();
}

static void default_walker(void* ptr, size_t size, int used, void* user)
{
	 heap_t* heap = (heap_t*)user;
	if (used == 1) {
		debug_print(k_print_warning, "Memory leak of size %llu bytes with callstack:\n", (uint64_t)size);

		callstack_print((void**)ptr, CALLSTACK_S, heap);
	}
}
