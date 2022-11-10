# include "frogger_game.h"

#include "ecs.h"
#include "fs.h"
#include "gpu.h"
#include "heap.h"
#include "render.h"
#include "timer_object.h"
#include "transform.h"
#include "wm.h"
#include "debug.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>


#define player_w 1.0f
#define player_h 1.0f
#define enemy_w 2.0f
#define enemy_h 1.0f
#define screen_w 160.0f/9.0f
#define screen_h 10.0f

#define max_speed 13
#define min_speed 3
#define max_gap 25000
#define min_gap 8000

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
	int row;
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

	int row_count;
	uint64_t row_timer[16];
	float row_speed[16];
	uint64_t row_gap[16];
} frogger_game_t;


// Private Functions

static void load_resources(frogger_game_t* game);
static void unload_resources(frogger_game_t* game);
static void draw_models(frogger_game_t* game);
static void spawn_camera(frogger_game_t* game);

static void load_player(frogger_game_t* game);
static void spawn_player(frogger_game_t* game, int index);
static void update_players(frogger_game_t* game);

static void load_enemy(frogger_game_t* game);
static void spawn_enemies(frogger_game_t* game);
static void spawn_enemy(frogger_game_t* game, int row);
static void update_enemies(frogger_game_t* game);

static bool check_collide(transform_t* player, transform_t* enemy);


// Game LEVEL

frogger_game_t* frogger_game_create(heap_t* heap, fs_t* fs, wm_window_t* window, render_t* render){
	frogger_game_t* game = heap_alloc(heap, sizeof(frogger_game_t), 8);
	game->heap = heap;
	game->fs = fs;
	game->window = window;
	game->render = render;

	game->timer = timer_object_create(heap, NULL);
	srand((uint32_t)time(NULL));

	game->ecs = ecs_create(heap);
	game->transform_type = ecs_register_component_type(game->ecs, "transform", sizeof(transform_component_t), _Alignof(transform_component_t));
	game->camera_type = ecs_register_component_type(game->ecs, "camera", sizeof(camera_component_t), _Alignof(camera_component_t));
	game->model_type = ecs_register_component_type(game->ecs, "model", sizeof(model_component_t), _Alignof(model_component_t));
	game->player_type = ecs_register_component_type(game->ecs, "player", sizeof(player_component_t), _Alignof(player_component_t));
	game->enemy_type = ecs_register_component_type(game->ecs, "enemy", sizeof(enemy_component_t), _Alignof(enemy_component_t));
	game->name_type = ecs_register_component_type(game->ecs, "name", sizeof(name_component_t), _Alignof(name_component_t));
	
	game->row_count = 3;
	for (int i = 0; i < game->row_count; i++) {
		game->row_timer[i] = 0;
		game->row_speed[i] = (float)(rand() % (max_speed - min_speed + 1) + min_speed);
		game->row_gap[i] = rand() % (max_gap - min_gap + 1) + min_gap;
	}

	load_resources(game);
	spawn_player(game, 0);
	spawn_camera(game);

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
	spawn_enemies(game);

	update_players(game);
	update_enemies(game);

	draw_models(game);
	render_push_done(game->render);
}

static void load_resources(frogger_game_t* game) {
	game->vertex_shader_work = fs_read(game->fs, "shaders/triangle.vert.spv", game->heap, false, false);
	game->fragment_shader_work = fs_read(game->fs, "shaders/triangle.frag.spv", game->heap, false, false);
	
	load_player(game);
	load_enemy(game);
}

static void unload_resources(frogger_game_t* game) {
	fs_work_destroy(game->fragment_shader_work);
	fs_work_destroy(game->vertex_shader_work);
}

static void draw_models(frogger_game_t* game) {
	uint64_t k_camera_query_mask = (1ULL << game->camera_type);
	for (ecs_query_t camera_query = ecs_query_create(game->ecs, k_camera_query_mask);
		ecs_query_is_valid(game->ecs, &camera_query);
		ecs_query_next(game->ecs, &camera_query))
	{
		camera_component_t* camera_comp = ecs_query_get_component(game->ecs, &camera_query, game->camera_type);

		uint64_t k_model_query_mask = (1ULL << game->transform_type) | (1ULL << game->model_type);
		for (ecs_query_t query = ecs_query_create(game->ecs, k_model_query_mask);
			ecs_query_is_valid(game->ecs, &query);
			ecs_query_next(game->ecs, &query))
		{
			transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
			model_component_t* model_comp = ecs_query_get_component(game->ecs, &query, game->model_type);
			ecs_entity_ref_t entity_ref = ecs_query_get_entity(game->ecs, &query);

			struct
			{
				mat4f_t projection;
				mat4f_t model;
				mat4f_t view;
			} uniform_data;
			uniform_data.projection = camera_comp->projection;
			uniform_data.view = camera_comp->view;
			transform_to_matrix(&transform_comp->transform, &uniform_data.model);
			gpu_uniform_buffer_info_t uniform_info = { .data = &uniform_data, sizeof(uniform_data) };

			render_push_model(game->render, &entity_ref, model_comp->mesh_info, model_comp->shader_info, &uniform_info);
		}
	}
}

static void spawn_camera(frogger_game_t* game)
{
	uint64_t k_camera_ent_mask =
		(1ULL << game->camera_type) |
		(1ULL << game->name_type);
	game->camera_ent = ecs_entity_add(game->ecs, k_camera_ent_mask);

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "camera");

	camera_component_t* camera_comp = ecs_entity_get_component(game->ecs, game->camera_ent, game->camera_type, true);
	mat4f_make_orthographic(&camera_comp->projection, -screen_h, screen_h, -screen_w, screen_w,  0.1f, 100.0f);
	//mat4f_make_perspective(&camera_comp->projection, (float)M_PI / 2.0f, 16.0f / 9.0f, 0.1f, 100.0f);

	vec3f_t eye_pos = vec3f_scale(vec3f_forward(), -5.0f);
	vec3f_t forward = vec3f_forward();
	vec3f_t up = vec3f_up();
	mat4f_make_lookat(&camera_comp->view, &eye_pos, &forward, &up);
}


// Player LEVEL

static void load_player(frogger_game_t* game) {
	game->player_shader = (gpu_shader_info_t)
	{
		.vertex_shader_data = fs_work_get_buffer(game->vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(game->vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(game->fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(game->fragment_shader_work),
		.uniform_buffer_count = 1,
	};

	static vec3f_t cube_verts[] =
	{
		{ -player_h, -player_w,  player_h }, { 0.0f, 1.0f,  0.0f },
		{  player_h, -player_w,  player_h }, { 0.0f, 1.0f,  0.0f },
		{  player_h,  player_w,  player_h }, { 0.0f, 1.0f,  0.0f },
		{ -player_h,  player_w,  player_h }, { 0.0f, 1.0f,  0.0f },
		{ -player_h, -player_w, -player_h }, { 0.0f, 1.0f,  0.0f },
		{  player_h, -player_w, -player_h }, { 0.0f, 1.0f,  0.0f },
		{  player_h,  player_w, -player_h }, { 0.0f, 1.0f,  0.0f },
		{ -player_h,  player_w, -player_h }, { 0.0f, 1.0f,  0.0f },
	};
	static uint16_t cube_indices[] =
	{
		0, 1, 2,
		2, 3, 0,
		1, 5, 6,
		6, 2, 1,
		7, 6, 5,
		5, 4, 7,
		4, 0, 3,
		3, 7, 4,
		4, 5, 1,
		1, 0, 4,
		3, 2, 6,
		6, 7, 3
	};
	game->player_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = cube_verts,
		.vertex_data_size = sizeof(cube_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
}


static void spawn_player(frogger_game_t* game, int index)
{
	uint64_t k_player_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->player_type) |
		(1ULL << game->name_type);
	game->player_ent = ecs_entity_add(game->ecs, k_player_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);
	transform_comp->transform.translation.y = (float)index * 5.0f;
	transform_comp->transform.translation.z = screen_h - player_h;

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "player");

	player_component_t* player_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->player_type, true);
	player_comp->index = index;

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->player_ent, game->model_type, true);
	model_comp->mesh_info = &game->player_mesh;
	model_comp->shader_info = &game->player_shader;
}

static void update_players(frogger_game_t* game)
{
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.01f;

	uint32_t key_mask = wm_get_key_mask(game->window);

	uint64_t k_query_mask = (1ULL << game->transform_type) | (1ULL << game->player_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		player_component_t* player_comp = ecs_query_get_component(game->ecs, &query, game->player_type);

		if (player_comp->index && transform_comp->transform.translation.z > 1.0f)
		{
			ecs_entity_remove(game->ecs, ecs_query_get_entity(game->ecs, &query), false);
		}

		transform_t move;
		transform_identity(&move);
		if (key_mask & k_key_up)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), -dt));
		}
		if (key_mask & k_key_down)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_up(), dt));
		}
		if (key_mask & k_key_left)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), -dt));
		}
		if (key_mask & k_key_right)
		{
			move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), dt));
		}
		transform_multiply(&transform_comp->transform, &move);

		// goal
		if (transform_comp->transform.translation.z < -screen_h + player_h ) {
			transform_comp->transform.translation.z = screen_h -player_h;
			transform_comp->transform.translation.y = 0.0f;
		}

		// out of bound
		if (transform_comp->transform.translation.y > screen_w - player_w) {
			transform_comp->transform.translation.y = screen_w - player_w;
		}
		else if (transform_comp->transform.translation.y < -screen_w + player_w) {
			transform_comp->transform.translation.y = -screen_w + player_w;
		}
		else if (transform_comp->transform.translation.z > screen_h - player_h) {
			transform_comp->transform.translation.z = screen_h - player_h;
		}
		//else if (transform_comp->transform.translation.z < -screen_h + player_h) {
		//	transform_comp->transform.translation.z = -screen_h + player_h;
		//}
	}
}


// Enemy LEVEL

static void load_enemy(frogger_game_t* game) {
	game->enemy_shader = (gpu_shader_info_t)
	{
		.vertex_shader_data = fs_work_get_buffer(game->vertex_shader_work),
		.vertex_shader_size = fs_work_get_size(game->vertex_shader_work),
		.fragment_shader_data = fs_work_get_buffer(game->fragment_shader_work),
		.fragment_shader_size = fs_work_get_size(game->fragment_shader_work),
		.uniform_buffer_count = 1,
	};

	// random a color for all sides
	vec3f_t color = { (float)rand() / (float)RAND_MAX, 
		(float)rand() / (float)RAND_MAX, (float)rand() / (float)RAND_MAX };

	static vec3f_t cube_verts[] =
	{
		{ -enemy_h, -enemy_w,  enemy_h }, { 0.0f, 1.0f,  0.0f },
		{  enemy_h, -enemy_w,  enemy_h }, { 0.0f, 1.0f,  0.0f },
		{  enemy_h,  enemy_w,  enemy_h }, { 0.0f, 1.0f,  0.0f },
		{ -enemy_h,  enemy_w,  enemy_h }, { 0.0f, 1.0f,  0.0f },
		{ -enemy_h, -enemy_w, -enemy_h }, { 0.0f, 1.0f,  0.0f },
		{  enemy_h, -enemy_w, -enemy_h }, { 0.0f, 1.0f,  0.0f },
		{  enemy_h,  enemy_w, -enemy_h }, { 0.0f, 1.0f,  0.0f },
		{ -enemy_h,  enemy_w, -enemy_h }, { 0.0f, 1.0f,  0.0f },
	};

	for (int i = 1; i < 16; i += 2) {
		cube_verts[i] = color;
	}

	static uint16_t cube_indices[] =
	{
		0, 1, 2,
		2, 3, 0,
		1, 5, 6,
		6, 2, 1,
		7, 6, 5,
		5, 4, 7,
		4, 0, 3,
		3, 7, 4,
		4, 5, 1,
		1, 0, 4,
		3, 2, 6,
		6, 7, 3
	};
	game->enemy_mesh = (gpu_mesh_info_t)
	{
		.layout = k_gpu_mesh_layout_tri_p444_c444_i2,
		.vertex_data = cube_verts,
		.vertex_data_size = sizeof(cube_verts),
		.index_data = cube_indices,
		.index_data_size = sizeof(cube_indices),
	};
}

static void spawn_enemies(frogger_game_t* game) {
	uint64_t time = timer_object_get_ms(game->timer);

	for (int i = 0; i < game->row_count; i++) {
		if (game->row_timer[i] < time) {
			spawn_enemy(game, i);
			game->row_timer[i] += game->row_gap[i] / (uint64_t)game->row_speed[i];
		}
	}

}

static void spawn_enemy(frogger_game_t* game, int row) {
	uint64_t k_enemy_ent_mask =
		(1ULL << game->transform_type) |
		(1ULL << game->model_type) |
		(1ULL << game->enemy_type) |
		(1ULL << game->name_type);
	game->enemy_ent = ecs_entity_add(game->ecs, k_enemy_ent_mask);

	transform_component_t* transform_comp = ecs_entity_get_component(game->ecs, game->enemy_ent, game->transform_type, true);
	transform_identity(&transform_comp->transform);
	transform_comp->transform.translation.y = ((row%2 == 1) ? -1 : 1) * screen_w;
	transform_comp->transform.translation.z = -5.0f + (float)row * 5.0f;

	name_component_t* name_comp = ecs_entity_get_component(game->ecs, game->enemy_ent, game->name_type, true);
	strcpy_s(name_comp->name, sizeof(name_comp->name), "enemy");

	enemy_component_t* enemy_comp = ecs_entity_get_component(game->ecs, game->enemy_ent, game->enemy_type, true);
	enemy_comp->row = row;

	model_component_t* model_comp = ecs_entity_get_component(game->ecs, game->enemy_ent, game->model_type, true);
	model_comp->mesh_info = &game->enemy_mesh;
	model_comp->shader_info = &game->enemy_shader;
}

static void update_enemies(frogger_game_t* game) {
	float dt = (float)timer_object_get_delta_ms(game->timer) * 0.001f;

	uint32_t key_mask = wm_get_key_mask(game->window);

	uint64_t k_query_mask = (1ULL << game->transform_type) | (1ULL << game->enemy_type);

	for (ecs_query_t query = ecs_query_create(game->ecs, k_query_mask);
		ecs_query_is_valid(game->ecs, &query);
		ecs_query_next(game->ecs, &query))
	{
		transform_component_t* transform_comp = ecs_query_get_component(game->ecs, &query, game->transform_type);
		enemy_component_t* enemy_comp = ecs_query_get_component(game->ecs, &query, game->enemy_type);

		transform_t move;
		transform_identity(&move);
		move.translation = vec3f_add(move.translation, vec3f_scale(vec3f_right(), 
					((enemy_comp->row%2 == 1) ? 1 : -1)*dt*game->row_speed[enemy_comp->row]));

		transform_multiply(&transform_comp->transform, &move);

		// delete out of bound object
		if (transform_comp->transform.translation.y < -screen_w - enemy_w ||
			transform_comp->transform.translation.y > screen_w + enemy_w) {
			//transform_comp->transform.translation.y = -1 * transform_comp->transform.translation.y;
			ecs_entity_remove(game->ecs, ecs_query_get_entity(game->ecs, &query), false);
		}


		uint64_t k_player_query_mask = (1ULL << game->transform_type) | (1ULL << game->player_type);
		for (ecs_query_t player_query = ecs_query_create(game->ecs, k_player_query_mask);
			ecs_query_is_valid(game->ecs, &player_query);
			ecs_query_next(game->ecs, &player_query)) {

			transform_component_t* transform_comp_player = 
				ecs_query_get_component(game->ecs, &player_query, game->transform_type);

			if (check_collide(&(transform_comp_player->transform), &(transform_comp->transform))) {
				//debug_print(k_print_info, "Collide\n");
				transform_comp_player->transform.translation.z = screen_h - player_h;
				transform_comp_player->transform.translation.y = 0.0f;
			}
		}

	}
}


static bool check_collide(transform_t* player, transform_t* enemy) {
	return player->translation.y - player_w < enemy->translation.y + enemy_w
		&& player->translation.y + player_w > enemy->translation.y - enemy_w
		&& player->translation.z - player_h < enemy->translation.z + enemy_h
		&& player->translation.z + player_h > enemy->translation.z - enemy_h;
}
