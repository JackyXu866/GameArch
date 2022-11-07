# include "frogger_game.h"

#include "ecs.h"
#include "fs.h"
#include "gpu.h"
#include "heap.h"
#include "render.h"
#include "timer_object.h"
#include "transform.h"
#include "wm.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>

typedef struct transform_component_t
{
	transform_t transform;
} transform_component_t;

typedef struct camera_component_t
{
	mat4f_t projection;
	mat4f_t view;
} camera_component_t;

typedef struct model_component_t
{
	gpu_mesh_info_t* mesh_info;
	gpu_shader_info_t* shader_info;
} model_component_t;

typedef struct player_component_t
{
	int index;
} player_component_t;

typedef struct enemy_component_t
{
	int index;
} enemy_component_t;


typedef struct name_component_t
{
	char name[32];
} name_component_t;

typedef struct frogger_game_t {
	heap_t* heap;
	fs_t* fs;
	wm_window_t* window;
	render_t* render;

	timer_object_t* timer;

	ecs_t* ecs;
	int transform_type;
	int camera_type;
	int model_type;
	int player_type;
	int enemy_type;
	int name_type;

	ecs_entity_ref_t player_ent;
	ecs_entity_ref_t enemy_ent;
	ecs_entity_ref_t camera_ent;

	gpu_mesh_info_t player_mesh;
	gpu_shader_info_t player_shader;

	gpu_mesh_info_t enemy_mesh;
	gpu_shader_info_t enemy_shader;

	fs_work_t* vertex_shader_work;
	fs_work_t* fragment_shader_work;
} frogger_game_t;


// private functions
void load_resources(frogger_game_t* game);


frogger_game_t* frogger_game_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render){
	frogger_game_t* game = heap_alloc(heap, sizeof(frogger_game_t), 8);
	game->heap = heap;
	game->fs = fs;
	game->window = window;
	game->render = render;

	game->timer = timer_object_create(heap, NULL);

	game->ecs = ecs_create(heap);
	game->transform_type = ecs_register_component_type(game->ecs, "transform", sizeof(transform_component_t), _Alignof(transform_component_t));
	game->camera_type = ecs_register_component_type(game->ecs, "camera", sizeof(camera_component_t), _Alignof(camera_component_t));
	game->model_type = ecs_register_component_type(game->ecs, "model", sizeof(model_component_t), _Alignof(model_component_t));
	game->player_type = ecs_register_component_type(game->ecs, "player", sizeof(player_component_t), _Alignof(player_component_t));
	game->enemy_type = ecs_register_component_type(game->ecs, "enemy", sizeof(enemy_component_t), _Alignof(enemy_component_t));
	game->name_type = ecs_register_component_type(game->ecs, "name", sizeof(name_component_t), _Alignof(name_component_t));

	load_resources(game);

	return game;
}

void frogger_game_destroy(frogger_game_t* game) {
	ecs_destroy(game->ecs);
	timer_object_destroy(game->timer);
	unload_resources(game);
	heap_free(game->heap, game);
}

void frogger_game_update(frogger_game_t* game) {
	timer_object_update(game->timer);
	ecs_update(game->ecs);
	update_players(game);
	draw_models(game);
	render_push_done(game->render);
}