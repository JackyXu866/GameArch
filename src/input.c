#include "input.h"

#include <stdint.h>

#include "wm.h"
#include "heap.h"
#include "debug.h"

#include <Xinput.h>
#include <math.h>

#define MAX_CONTROLLERS 8

typedef struct input_t {
	uint32_t key_mask;
	uint32_t keymap;
	
	uint32_t controllermap[MAX_CONTROLLERS];
	XINPUT_STATE state[MAX_CONTROLLERS];
	bool active_controller[MAX_CONTROLLERS];
	
	float pointer_x;
	float pointer_y;

	float horizontal;
	float vertical;
} input_t;


const struct
{
	int virtual_key;
	int ga_key;
}

k_key_map[] = {
	{.virtual_key = VK_LEFT, .ga_key = k_key_left, },
	{.virtual_key = VK_RIGHT, .ga_key = k_key_right, },
	{.virtual_key = VK_UP, .ga_key = k_key_up, },
	{.virtual_key = VK_DOWN, .ga_key = k_key_down, },

	{.virtual_key = 0x57, .ga_key = k_key_up},		// W
	{.virtual_key = 0x41, .ga_key = k_key_left},	// A
	{.virtual_key = 0x53, .ga_key = k_key_down},	// S
	{.virtual_key = 0x44, .ga_key = k_key_right},	// D

	{.virtual_key = VK_ESCAPE, .ga_key = k_button_cancel},
	{.virtual_key = VK_RETURN, .ga_key = k_button_action},
	{.virtual_key = VK_SPACE, .ga_key = k_button_extra1},
	{.virtual_key = VK_SHIFT, .ga_key = k_button_extra2},
};

const struct
{
	int gamepad;
	int ga_key;
}

k_gamepad_map[] = {
	{.gamepad = XINPUT_GAMEPAD_DPAD_UP, .ga_key = k_key_up},
	{.gamepad = XINPUT_GAMEPAD_DPAD_DOWN, .ga_key = k_key_down},
	{.gamepad = XINPUT_GAMEPAD_DPAD_LEFT, .ga_key = k_key_left},
	{.gamepad = XINPUT_GAMEPAD_DPAD_RIGHT, .ga_key = k_key_right},

	{.gamepad = XINPUT_GAMEPAD_A, .ga_key = k_button_action},
	{.gamepad = XINPUT_GAMEPAD_B, .ga_key = k_button_cancel},
	{.gamepad = XINPUT_GAMEPAD_X, .ga_key = k_button_extra1},
	{.gamepad = XINPUT_GAMEPAD_Y, .ga_key = k_button_extra2},

	{.gamepad = XINPUT_GAMEPAD_RIGHT_THUMB, .ga_key = k_fire_3},
};

void keyboard_mouse_update(input_t* input, UINT uMsg, WPARAM wParam);
void gamepad_update(input_t* input);

input_t* input_create(heap_t* heap) {
	input_t* i = heap_alloc(heap, sizeof(input_t), 8);
	i->key_mask = 0;
	i->keymap = 0;
	i->horizontal = 1.0f;
	i->vertical = 1.0f;
	i->pointer_x = 1.0f;
	i->pointer_y = 1.0f;
	
	for (int j = 0; j < MAX_CONTROLLERS; j++) {
		XINPUT_STATE state;
		ZeroMemory(&state, sizeof(XINPUT_STATE));
		DWORD result = XInputGetState(j, &state);
		if (result == ERROR_SUCCESS) {
			i->state[j] = state;
			i->controllermap[j] = 0;
			i->active_controller[j] = true;
			debug_print(k_print_info, "Found controller %d\n", j);
		}
		else {
			i->active_controller[j] = false;
		}
	}
	return i;
}

void input_destroy(heap_t* heap, input_t* i) {
	heap_free(heap, i);
}

void update_key(input_t* input, UINT uMsg, WPARAM wParam) {
	keyboard_mouse_update(input, uMsg, wParam);
	
	gamepad_update(input);
	
	input->key_mask = input->keymap;
	for (int i = 0; i < MAX_CONTROLLERS; i++) {
		if (input->active_controller[i])
			input->key_mask |= input->controllermap[i];
	}
}

void keyboard_mouse_update(input_t* input, UINT uMsg, WPARAM wParam) {
	switch (uMsg) {
	case WM_KEYDOWN:
		for (int i = 0; i < _countof(k_key_map); ++i) {
			if (k_key_map[i].virtual_key == wParam) {
				input->keymap |= k_key_map[i].ga_key;
				break;
			}
		}
		break;
	case WM_KEYUP:
		for (int i = 0; i < _countof(k_key_map); ++i) {
			if (k_key_map[i].virtual_key == wParam) {
				input->keymap &= ~k_key_map[i].ga_key;
				break;
			}
		}
		break;
	case WM_LBUTTONDOWN:
		input->keymap |= k_fire_1;
		break;
	case WM_LBUTTONUP:
		input->keymap &= ~k_fire_1;
		break;
	case WM_RBUTTONDOWN:
		input->keymap |= k_fire_2;
		break;
	case WM_RBUTTONUP:
		input->keymap &= ~k_fire_2;
		break;
	case WM_MBUTTONDOWN:
		input->keymap |= k_fire_3;
		break;
	case WM_MBUTTONUP:
		input->keymap &= ~k_fire_3;
		break;
	}
}

void gamepad_update(input_t* input) {
	for (int i = 0; i < MAX_CONTROLLERS; i++) {
		XInputGetState(i, &input->state[i]);
		XINPUT_STATE state = input->state[i];
		if (state.dwPacketNumber == 0) {
			if (input->active_controller[i]) {
				debug_print(k_print_info, "Lost controller %d\n", i);
				input->active_controller[i] = false;
			}
			continue;
		}
		else if (!input->active_controller[i]) {
			debug_print(k_print_info, "Found controller %d\n", i);
			input->active_controller[i] = true;
		}
		
		for (int j = 0; j < _countof(k_gamepad_map); j++) {
			if (state.Gamepad.wButtons & k_gamepad_map[j].gamepad) {
				input->controllermap[i] |= k_gamepad_map[j].ga_key;
			}
			else {
				input->controllermap[i] &= ~k_gamepad_map[j].ga_key;
			}
		}

		// triggers
		if (state.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
			input->controllermap[i] |= k_fire_1;
		}
		else {
			input->controllermap[i] &= ~k_fire_1;
		}

		if (state.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD) {
			input->controllermap[i] |= k_fire_2;
		}
		else {
			input->controllermap[i] &= ~k_fire_2;
		}

		// left thumbsticks
		if (state.Gamepad.sThumbLX < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) {
			input->controllermap[i] |= k_key_left;
			input->horizontal = state.Gamepad.sThumbLX / 32767.0f;
		}
		else {
			input->controllermap[i] &= ~k_key_left;
			input->horizontal = 1.0f;
		}

		if (state.Gamepad.sThumbLX > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) {
			input->controllermap[i] |= k_key_right;
			input->horizontal = state.Gamepad.sThumbLX / 32767.0f;
		}
		else {
			input->controllermap[i] &= ~k_key_right;
			input->horizontal = 1.0f;
		}

		if (state.Gamepad.sThumbLY < -XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) {
			input->controllermap[i] |= k_key_down;
			input->vertical = state.Gamepad.sThumbLY / 32767.0f;
		}
		else {
			input->controllermap[i] &= ~k_key_down;
			input->vertical = 1.0f;
		}

		if (state.Gamepad.sThumbLY > XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE) {
			input->controllermap[i] |= k_key_up;
			input->vertical = state.Gamepad.sThumbLY / 32767.0f;
		}
		else {
			input->controllermap[i] &= ~k_key_up;
			input->vertical = 1.0f;
		}
	}
}

void update_pointer(input_t* input, UINT uMsg, RECT rect) {
	if (uMsg == WM_MOUSEMOVE) {
		// Relative mouse movement in four steps:
		// 1. Get current mouse position (old_cursor).
		// 2. Move mouse back to center of window.
		// 3. Get current mouse position (new_cursor).
		// 4. Compute relative movement (old_cursor - new_cursor).
		POINT old_cursor;
		GetCursorPos(&old_cursor);
		
		SetCursorPos(
			(rect.left + rect.right) / 2,
			(rect.top + rect.bottom) / 2);
		
		POINT new_cursor;
		GetCursorPos(&new_cursor);
		
		input->pointer_x = (float)old_cursor.x - new_cursor.x;
		input->pointer_y = (float)old_cursor.y - new_cursor.y;
	}

	// Gamepad
	for (int i = 0; i < MAX_CONTROLLERS; i++) {
		XInputGetState(i, &input->state[i]);
		XINPUT_STATE state = input->state[i];
		if (state.dwPacketNumber == 0) {
			if (input->active_controller[i]) {
				debug_print(k_print_info, "Lost controller %d\n", i);
				input->active_controller[i] = false;
			}
			continue;
		}
		else if (!input->active_controller[i]) {
			debug_print(k_print_info, "Found controller %d\n", i);
			input->active_controller[i] = true;
		}
		
		float sensitivity = (float)(rect.right - rect.left) / 10.0f;
		
		if (abs(state.Gamepad.sThumbRX) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) {
			input->pointer_x = (float)state.Gamepad.sThumbRX / 32767.0f * sensitivity;
		}
		
		if (abs(state.Gamepad.sThumbRY) > XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE) {
			input->pointer_y = (float)state.Gamepad.sThumbRY / 32767.0f * sensitivity;
			
		}

	}

}

void set_vibration(input_t* input, int i, uint16_t left, uint16_t right) {
	if (input->active_controller[i] == false) {
		debug_print(k_print_warning, "Controller %d is not connected", i);
		return;
	}

	XINPUT_VIBRATION vibration;
	ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));
	vibration.wLeftMotorSpeed = left;
	vibration.wRightMotorSpeed = right;
	XInputSetState(i, &vibration);
}

uint32_t input_get_key_mask(input_t* input) {
	return input->key_mask;
}

void input_get_mouse_move(input_t* input, float* x, float* y) {
	*x = input->pointer_x;
	*y = input->pointer_y;
}

float input_get_horizontal(input_t* input) {
	return (float)fabs(input->horizontal);
}

float input_get_vertical(input_t* input) {
	return (float)fabs(input->vertical);
}