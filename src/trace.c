#include "trace.h"

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "timer.h"
#include "atomic.h"
#include "queue.h"
#include "debug.h"
#include "fs.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef struct thread_queue_t thread_queue_t;
typedef struct duration_t duration_t;

typedef struct trace_t {
	uint32_t max_duration;
	char* path;
	heap_t* heap;
	duration_t** list_durations;
	uint32_t num_duration;
	thread_queue_t** thread_q;
	uint32_t num_q;
	bool start;
} trace_t;

typedef struct duration_t {
	char* name;
	uint32_t pid;
	uint32_t tid;
	uint64_t ts;
	char ph;
} duration_t;

// store the name of duration for each thread
typedef struct thread_queue_t {
	uint32_t tid;
	char** q;
	uint32_t head_index;
	uint32_t end_index;
}thread_queue_t;

thread_queue_t* find_queue(trace_t* trace);
void thread_queue_push(trace_t* trace, thread_queue_t* q, const char* name);
char* thread_queue_pop(thread_queue_t* q);
void destroy_thread_queue(heap_t* heap, thread_queue_t* q);

trace_t* trace_create(heap_t* heap, int event_capacity)
{
	trace_t* trace = heap_alloc(heap, sizeof(trace_t), 8);
	trace->max_duration = event_capacity;
	trace->heap = heap;
	trace->list_durations = heap_alloc(heap, event_capacity * sizeof(duration_t*), 8);
	trace->num_duration = 0;
	trace->thread_q = heap_alloc(heap, event_capacity * sizeof(thread_queue_t*), 8);
	trace->num_q = 0;
	trace->start = false;

	return trace;
}

void trace_destroy(trace_t* trace)
{
	for (uint32_t i = 0; i < trace->num_duration; i++) {
		heap_free(trace->heap, (trace->list_durations)[i]->name);
		heap_free(trace->heap, (trace->list_durations)[i]);
	}
	heap_free(trace->heap, trace->list_durations);

	for (uint32_t i = 0; i < trace->num_q; i++) {
		destroy_thread_queue(trace->heap, (trace->thread_q)[i]);
	}
	heap_free(trace->heap, trace->thread_q);

	heap_free(trace->heap, trace->path);

	heap_free(trace->heap, trace);
}

void trace_duration_push(trace_t* trace, const char* name)
{
	if (!trace->start) return;

	uint32_t old_num = atomic_increment(&(trace->num_duration));
	if (old_num >= trace->max_duration) {
		debug_print(k_print_warning, "Exceed max duration count");
		return;
	}

	duration_t* tmp_duration = heap_alloc(trace->heap, sizeof(duration_t), 8);
	// +1 to include '\0'
	tmp_duration->name = heap_alloc(trace->heap, strlen(name) + 1, 8);
	strncpy_s(tmp_duration->name, strlen(name) + 1, name, strlen(name)+1);
	tmp_duration->pid = GetCurrentProcessId();
	tmp_duration->tid = GetCurrentThreadId();
	tmp_duration->ts = timer_ticks_to_us(timer_get_ticks());
	tmp_duration->ph = 'B';

	(trace->list_durations)[old_num] = tmp_duration;

	thread_queue_t* tmp_queue = find_queue(trace);
	thread_queue_push(trace, tmp_queue, tmp_duration->name);
}

void trace_duration_pop(trace_t* trace)
{
	if (!trace->start) return;

	thread_queue_t* tmp_queue = find_queue(trace);
	char* name = thread_queue_pop(tmp_queue);
	if (name == NULL) {
		debug_print(k_print_warning, "Nothing in the thread queue to pop.");
		return;
	}

	uint32_t old_num = atomic_increment(&(trace->num_duration));
	if (old_num >= trace->max_duration) {
		debug_print(k_print_warning, "Exceed max duration count");
		return;
	}

	duration_t* tmp_duration = heap_alloc(trace->heap, sizeof(duration_t), 8);
	tmp_duration->name = heap_alloc(trace->heap, strlen(name) + 1, 8);
	strncpy_s(tmp_duration->name, strlen(name) + 1, name, strlen(name) + 1);
	tmp_duration->pid = GetCurrentProcessId();
	tmp_duration->tid = GetCurrentThreadId();
	tmp_duration->ts = timer_ticks_to_us(timer_get_ticks());
	tmp_duration->ph = 'E';

	heap_free(trace->heap, name);

	(trace->list_durations)[old_num] = tmp_duration;

}

void trace_capture_start(trace_t* trace, const char* path)
{
	trace->start = true;
	trace->path = heap_alloc(trace->heap, strlen(path) + 1, 8);
	strncpy_s(trace->path, strlen(path) + 1, path, strlen(path));
}

void trace_capture_stop(trace_t* trace)
{
	trace->start = false;

	fs_t* file = fs_create(trace->heap, 16);

	uint32_t size = 200 * (trace->num_duration + 1);
	char* buffer = heap_alloc(trace->heap, size, 8);

	char* head = "{\n\t\"displayTimeUnit\": \"ns\", \"traceEvents\" : [\n";
	strncat_s(buffer, size, head, strlen(head));

	for (uint32_t i = 0; i < trace->num_duration; i++) {
		char buf[200];
		duration_t* tmp = (trace->list_durations)[i];
		sprintf_s(buf, 200, "\t\t{\"name\": \"%s\",\"ph\": \"%c\",\"pid\":%u,\"tid\":\"%u\",\"ts\":%llu}%c\n\0",
			tmp->name, tmp->ph, tmp->pid, tmp->tid, tmp->ts, (i == trace->num_duration - 1) ? ' ' : ',');
		strncat_s(buffer, size, buf, strlen(buf));
	}

	char* tail = "\t]\n}\n";
	strncat_s(buffer, size, tail, strlen(tail));

	fs_work_t* worker = fs_write(file, trace->path, buffer, strlen(buffer), false);

	fs_work_wait(worker);
	heap_free(trace->heap, buffer);
}

thread_queue_t* find_queue(trace_t* trace) {
	uint32_t tid = GetCurrentThreadId();
	for (uint32_t i = 0; i < trace->num_q; i++) {
		if ((trace->thread_q)[i]->tid == tid)
			return (trace->thread_q)[i];
	}

	// create thread_queue if not found
	uint32_t old_num = atomic_increment(&(trace->num_q));
	if (old_num >= trace->max_duration) {
		debug_print(k_print_error, "Thread exceed limit");
		return NULL;
	}
	
	thread_queue_t* tmp_q = heap_alloc(trace->heap, sizeof(thread_queue_t), 8);
	tmp_q->q = heap_alloc(trace->heap, sizeof(char*), 8);
	tmp_q->tid = tid;
	tmp_q->head_index = 0;
	tmp_q->end_index = 0;
	(trace->thread_q)[old_num] = tmp_q;

	return tmp_q;
}

void thread_queue_push(trace_t* trace, thread_queue_t* q, const char* name) {
	if (q->end_index >= trace->max_duration) {
		debug_print(k_print_error, "Queue exceed limit");
		return;
	}
	(q->q)[q->end_index] = heap_alloc(trace->heap, strlen(name) + 1, 8);
	strncpy_s((q->q)[q->end_index], strlen(name) + 1, name, strlen(name)+1);
	q->end_index++;
}

// if nothing at queue, return NULL
char* thread_queue_pop(thread_queue_t* q) {
	if (q->end_index - q->head_index <= 0) return NULL;
	return (q->q)[q->head_index++];
}

// all names poped out should be freed by outside functions
void destroy_thread_queue(heap_t* heap, thread_queue_t* q) {
	for (uint32_t i = q->head_index; i < (q->end_index); i++) {
		heap_free(heap, (q->q)[i]);
	}
	heap_free(heap, q->q);
	heap_free(heap, q);
}
