#include "wm.h"

#include "debug.h"
#include "heap.h"

#include "input.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

typedef struct wm_window_t
{
	HWND hwnd;
	heap_t* heap;
	bool quit;
	bool has_focus;
	input_t* input;
} wm_window_t;


static LRESULT CALLBACK _window_proc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	wm_window_t* win = (wm_window_t*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (win)
	{
		update_key(win->input, uMsg, wParam);
		if (win->has_focus) {
			RECT window_rect;
			GetWindowRect(hwnd, &window_rect);
			update_pointer(win->input, uMsg, window_rect);
		}
		switch (uMsg)
		{
		case WM_ACTIVATEAPP:
			ShowCursor(!wParam);
			win->has_focus = wParam;
			break;

		case WM_CLOSE:
			win->quit = true;
			break;
		}
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

wm_window_t* wm_create(heap_t* heap)
{
	WNDCLASS wc =
	{
		.lpfnWndProc = _window_proc,
		.hInstance = GetModuleHandle(NULL),
		.lpszClassName = L"ga2022 window class",
	};
	RegisterClass(&wc);

	HWND hwnd = CreateWindowEx(
		0,
		wc.lpszClassName,
		L"GA 2022",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		NULL,
		NULL,
		wc.hInstance,
		NULL);

	if (!hwnd)
	{
		debug_print(
			k_print_warning,
			"Failed to create window!\n");
		return NULL;
	}

	wm_window_t* win = heap_alloc(heap, sizeof(wm_window_t), 8);
	win->has_focus = false;
	win->hwnd = hwnd;
	win->input = input_create(heap);
	win->quit = false;
	win->heap = heap;

	SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)win);

	// Windows are created hidden by default, so we
	// need to show it here.
	ShowWindow(hwnd, TRUE);

	return win;
}

bool wm_pump(wm_window_t* window)
{
	MSG msg = { 0 };
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	return window->quit;
}

input_t* wm_get_input(wm_window_t* window)
{
	return window->input;
}

uint32_t wm_get_key_mask(wm_window_t* window) {
	return input_get_key_mask(window->input);
}

void wm_get_mouse_move(wm_window_t* window, float* x, float* y)
{
	input_get_mouse_move(window->input, x, y);
}

void wm_destroy(wm_window_t* window)
{
	DestroyWindow(window->hwnd);
	input_destroy(window->heap, window->input);
	heap_free(window->heap, window);
}

void* wm_get_raw_window(wm_window_t* window)
{
	return window->hwnd;
}
