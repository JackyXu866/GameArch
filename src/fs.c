#include "fs.h"

#include "event.h"
#include "heap.h"
#include "queue.h"
#include "thread.h"
#include "lz4/lz4.h"
#include "debug.h"

#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct fs_t
{
	heap_t* heap;
	queue_t* file_queue;
	thread_t* file_thread;
	queue_t* compress_queue;
	thread_t* compress_thread;
} fs_t;

typedef enum fs_work_op_t
{
	k_fs_work_op_read,
	k_fs_work_op_write,
} fs_work_op_t;

typedef enum fs_comp_op_t
{
	k_fs_work_op_compress,
	k_fs_work_op_decompress,
} fs_comp_op_t;

typedef struct fs_work_t
{
	heap_t* heap;
	fs_work_op_t op;
	fs_comp_op_t op_comp;
	char path[1024];
	bool null_terminate;
	bool use_compression;
	void* buffer;
	size_t size;
	size_t size_comp;
	event_t* done;
	event_t* compress_done;
	int result;
} fs_work_t;

static int file_thread_func(void* user);
static int compress_thread_func(void* user);


fs_t* fs_create(heap_t* heap, int queue_capacity)
{
	fs_t* fs = heap_alloc(heap, sizeof(fs_t), 8);
	fs->heap = heap;
	fs->file_queue = queue_create(heap, queue_capacity);
	fs->file_thread = thread_create(file_thread_func, fs);

	fs->compress_queue = queue_create(heap, queue_capacity);
	fs->compress_thread = thread_create(compress_thread_func, fs);

	return fs;
}

void fs_destroy(fs_t* fs)
{
	queue_push(fs->file_queue, NULL);
	thread_destroy(fs->file_thread);
	queue_destroy(fs->file_queue);

	queue_push(fs->compress_queue, NULL);
	thread_destroy(fs->compress_thread);
	queue_destroy(fs->compress_queue);

	heap_free(fs->heap, fs);
}

fs_work_t* fs_read(fs_t* fs, const char* path, heap_t* heap, bool null_terminate, bool use_compression)
{
	fs_work_t* work = heap_alloc(fs->heap, sizeof(fs_work_t), 8);
	work->heap = heap;
	work->op = k_fs_work_op_read;
	strcpy_s(work->path, sizeof(work->path), path);
	work->buffer = NULL;
	work->size = 0;
	work->done = event_create();
	work->compress_done = event_create();
	work->result = 0;
	work->null_terminate = null_terminate;
	work->use_compression = use_compression;
	queue_push(fs->file_queue, work);
	return work;
}

fs_work_t* fs_write(fs_t* fs, const char* path, const void* buffer, size_t size, bool use_compression)
{
	fs_work_t* work = heap_alloc(fs->heap, sizeof(fs_work_t), 8);
	work->heap = fs->heap;
	work->op = k_fs_work_op_write;
	strcpy_s(work->path, sizeof(work->path), path);
	work->buffer = (void*)buffer;
	work->size = size;
	work->done = event_create();
	work->compress_done = event_create();
	work->result = 0;
	work->null_terminate = false;
	work->use_compression = use_compression;

	if (use_compression)
	{
		// HOMEWORK 2: Queue file write work on compression queue!
		work->op_comp = k_fs_work_op_compress;
		queue_push(fs->compress_queue, work);
		event_wait(work->compress_done);
		queue_push(fs->file_queue, work);
	}
	else
	{
		queue_push(fs->file_queue, work);
	}

	return work;
}

bool fs_work_is_done(fs_work_t* work)
{
	return work ? event_is_raised(work->done) : true;
}

void fs_work_wait(fs_work_t* work)
{
	if (work)
	{
		event_wait(work->done);
	}
}

int fs_work_get_result(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->result : -1;
}

void* fs_work_get_buffer(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->buffer : NULL;
}

size_t fs_work_get_size(fs_work_t* work)
{
	fs_work_wait(work);
	return work ? work->size : 0;
}

void fs_work_destroy(fs_work_t* work)
{
	if (work)
	{
		event_wait(work->done);
		event_destroy(work->done);
		heap_free(work->heap, work->buffer);
		heap_free(work->heap, work);
	}
}

static void file_read(fs_work_t* work, fs_t* fs)
{
	wchar_t wide_path[1024];
	if (MultiByteToWideChar(CP_UTF8, 0, work->path, -1, wide_path, sizeof(wide_path)) <= 0)
	{
		work->result = -1;
		event_signal(work->done);
		return;
	}

	HANDLE handle = CreateFile(wide_path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		work->result = GetLastError();
		event_signal(work->done);
		return;
	}

	if (!GetFileSizeEx(handle, (PLARGE_INTEGER)&work->size))
	{
		work->result = GetLastError();
		CloseHandle(handle);
		event_signal(work->done);
		return;
	}

	work->buffer = heap_alloc(work->heap, work->null_terminate ? work->size + 1 : work->size, 8);

	DWORD bytes_read = 0;
	if (!ReadFile(handle, work->buffer, (DWORD)work->size, &bytes_read, NULL))
	{
		work->result = GetLastError();
		CloseHandle(handle);
		event_signal(work->done);
		return;
	}

	work->size = bytes_read;
	CloseHandle(handle);

	if (work->use_compression)
	{
		// HOMEWORK 2: Queue file read work on decompression queue!
		work->op_comp = k_fs_work_op_decompress;
		queue_push(fs->compress_queue, work);
		event_wait(work->compress_done);
	}

	if (work->null_terminate)
	{
		((char*)work->buffer)[work->size] = 0;
	}

	event_signal(work->done);
}

static void file_write(fs_work_t* work)
{
	wchar_t wide_path[1024];
	if (MultiByteToWideChar(CP_UTF8, 0, work->path, -1, wide_path, sizeof(wide_path)) <= 0)
	{
		work->result = -1;
		event_signal(work->done);
		return;
	}

	HANDLE handle = CreateFile(wide_path, GENERIC_WRITE, FILE_SHARE_WRITE, NULL,
		CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (handle == INVALID_HANDLE_VALUE)
	{
		work->result = GetLastError();
		event_signal(work->done);
		return;
	}

	DWORD bytes_written = 0;
	DWORD size = (DWORD)(work->use_compression ? work->size_comp : work->size);
	if (!WriteFile(handle, work->buffer, size, &bytes_written, NULL))
	{
		work->result = GetLastError();
		CloseHandle(handle);
		event_signal(work->done);
		return;
	}

	if (work->use_compression) {
		heap_free(work->heap, work->buffer);
		work->size_comp = bytes_written;
	}
	else work->size = bytes_written;

	CloseHandle(handle);

	event_signal(work->done);
}

static void file_compress(fs_work_t* work) {
	int buffer_size = LZ4_compressBound((int)work->size);
	char* buffer_comp = heap_alloc(work->heap, buffer_size, 8);
	int comp_size = LZ4_compress_default(work->buffer, buffer_comp, (int)work->size, buffer_size);

	if (!comp_size) {
		debug_print(k_print_warning, "Compression failed\n");
		heap_free(work->heap, buffer_comp);
	}
	else {
		work->buffer = buffer_comp;
		work->size_comp = comp_size;
	}

	event_signal(work->compress_done);
}

static void file_decompress(fs_work_t* work) {
	// estimate max 255 times compressed size
	int buffer_size = 256 * (int)work->size;
	char* buffer_decomp = heap_alloc(work->heap, buffer_size, 8);
	int decomp_size = LZ4_decompress_safe((char*)work->buffer, buffer_decomp, (int)work->size, buffer_size);

	if (decomp_size < 0) {
		debug_print(k_print_warning, "Decompression failed\n");
	}

	heap_free(work->heap, work->buffer);

	work->buffer = buffer_decomp;
	work->size = (size_t)decomp_size;

	event_signal(work->compress_done);
}

static int file_thread_func(void* user)
{
	fs_t* fs = user;
	while (true)
	{
		fs_work_t* work = queue_pop(fs->file_queue);
		if (work == NULL)
		{
			break;
		}
		
		switch (work->op)
		{
		case k_fs_work_op_read:
			file_read(work, fs);
			break;
		case k_fs_work_op_write:
			file_write(work);
			break;
		}
	}
	return 0;
}

static int compress_thread_func(void* user) {
	fs_t* fs = user;
	while (true)
	{
		fs_work_t* work = queue_pop(fs->compress_queue);
		if (work == NULL)
		{
			break;
		}

		switch (work->op_comp)
		{
		case k_fs_work_op_compress:
			file_compress(work);
			break;
		case k_fs_work_op_decompress:
			file_decompress(work);
			break;
		}
	}
	return 0;
}
