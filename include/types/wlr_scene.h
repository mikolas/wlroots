#ifndef TYPES_WLR_SCENE_H
#define TYPES_WLR_SCENE_H

#include <wlr/types/wlr_scene.h>

struct wlr_scene *scene_node_get_root(struct wlr_scene_node *node);

void scene_node_get_size(struct wlr_scene_node *node, int *width, int *height);
void scene_node_opaque_region(struct wlr_scene_node *node, int x, int y,
	pixman_region32_t *opaque);
struct wlr_texture *scene_buffer_get_texture(struct wlr_scene_buffer *scene_buffer,
	struct wlr_renderer *renderer);
void scene_output_damage(struct wlr_scene_output *scene_output,
	const pixman_region32_t *damage);

void scene_surface_set_clip(struct wlr_scene_surface *surface, struct wlr_box *clip);

struct wlr_kfx_node_hook;
struct wlr_output;

struct wlr_kfx_node_hook *kfx_scene_node_get_hook(struct wlr_scene_node *node);
bool kfx_output_requires_two_pass(struct wlr_output *output);
bool kfx_output_get_dither(struct wlr_output *output);
bool kfx_output_consume_color_dirty(struct wlr_output *output);
float kfx_output_get_sdr_white_nits(struct wlr_output *output);
bool kfx_output_get_tonemap_max_pq(struct wlr_output *output, float *out_max_pq);

#endif
