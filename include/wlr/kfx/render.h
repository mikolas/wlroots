/*
 * This is an unstable interface of wlroots. No guarantees are made regarding
 * the future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_KFX_RENDER_H
#define WLR_KFX_RENDER_H

#include <pixman.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wlr/render/pass.h>
#include <wlr/util/box.h>

#define WLR_KFX_MAX_INPUTS 4
#define WLR_KFX_MAX_PARAMS_SIZE 256

struct wlr_buffer;
struct wlr_renderer;
struct wlr_kfx_shader;
struct wlr_kfx_target;

struct wlr_kfx_shader_layout {
	size_t n_inputs;
	size_t params_size;
};

struct wlr_kfx_draw_options {
	struct wlr_kfx_shader *shader;
	struct wlr_box dst_box;
	const pixman_region32_t *clip;
	struct wlr_kfx_target *inputs[WLR_KFX_MAX_INPUTS];
	const void *params;
	size_t params_len;
	enum wlr_render_blend_mode blend_mode;
};

bool wlr_kfx_is_supported(struct wlr_renderer *renderer);

struct wlr_kfx_shader *wlr_kfx_shader_create(struct wlr_renderer *renderer,
	const uint32_t *spirv, size_t len,
	const struct wlr_kfx_shader_layout *layout);
void wlr_kfx_shader_destroy(struct wlr_kfx_shader *shader);

void wlr_kfx_render_pass_add_draw(struct wlr_render_pass *pass,
	const struct wlr_kfx_draw_options *options);

struct wlr_render_pass *wlr_kfx_renderer_begin_buffer_pass(
	struct wlr_renderer *renderer, struct wlr_buffer *buffer,
	const struct wlr_buffer_pass_options *options, bool require_two_pass);

#endif
