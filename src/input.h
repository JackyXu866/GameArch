#pragma once
#include "heap.h"

#include <stdint.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

// Input Manager
// 
// This module is responsible for managing input from the user.
// It could support any keyboards, mouses and any Xinput controllers.
// The maximum number of controllers is defined by MAX_CONTROLLERS.

// Handle input events.
typedef struct input_t input_t;

// Action codes
enum
{
	k_move_left = 1 << 0,
	k_move_right = 1 << 1,
	k_move_up = 1 << 2,
	k_move_down = 1 << 3,
	
	k_button_cancel = 1 << 4,
	k_button_action = 1 << 5,
	k_button_extra1 = 1 << 6,
	k_button_extra2 = 1 << 7,
	
	k_fire_1 = 1 << 8,
	k_fire_2 = 1 << 9,
	k_fire_3 = 1 << 10,
};


// Create a new input handler.
input_t* input_create(heap_t* heap);

// Destroy a previously created input handler.
void input_destroy(heap_t* heap, input_t* input);

// Pump the input handler.
// This will refresh all the states of keyboards, mouses, and controllers.
void update_key(input_t* input, UINT uMsg, WPARAM wParam);

// Update any cursor-like control, including mouse and thumbsticks.
void update_pointer(input_t* input, UINT uMsg, RECT rect);

// Set vibration for ith controller.
void set_vibration(input_t* input, int i, uint16_t left, uint16_t right);

// Get the current state of the key.
uint32_t input_get_key_mask(input_t* input);

// Get the relative movement of cursor-like controls.
void input_get_mouse_move(input_t* input, float* x, float* y);

// Get the power of horizontal movement.
// Keyboard control would return 1.
float input_get_horizontal(input_t* input);

// Get the power of vertical movement.
// Keyboard control would return 1.
float input_get_vertical(input_t* input);