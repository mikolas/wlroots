/*
 * This is an unstable interface of wlroots. No guarantees are made regarding
 * the future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_KFX_SCENE_H
#define WLR_KFX_SCENE_H

#include <pixman.h>
#include <stdbool.h>
#include <wlr/util/addon.h>
#include <wlr/util/box.h>

struct wlr_kfx_target;
struct wlr_output;
struct wlr_render_pass;
struct wlr_scene_node;
struct wlr_scene_output;

enum wlr_kfx_hook_stage {
	WLR_KFX_BEFORE,
	WLR_KFX_INSTEAD,
	WLR_KFX_AFTER,
};

struct wlr_kfx_render_ctx {
	struct wlr_render_pass *render_pass;
	struct wlr_box dst_box;
	const pixman_region32_t *clip;
	struct wlr_scene_output *output;
};

struct wlr_kfx_node_hook;

struct wlr_kfx_node_hook_impl {
	void (*render)(struct wlr_kfx_node_hook *hook, enum wlr_kfx_hook_stage stage,
		const struct wlr_kfx_render_ctx *ctx);
};

struct wlr_kfx_node_hook {
	enum wlr_kfx_hook_stage stage;
	const struct wlr_kfx_node_hook_impl *impl;
	struct wlr_scene_node *node;

	struct {
		struct wlr_addon addon;
		bool active;
	} WLR_PRIVATE;
};

bool wlr_kfx_node_hook_init(struct wlr_kfx_node_hook *hook,
	struct wlr_scene_node *leaf, enum wlr_kfx_hook_stage stage,
	const struct wlr_kfx_node_hook_impl *impl);
void wlr_kfx_node_hook_finish(struct wlr_kfx_node_hook *hook);

void wlr_kfx_output_set_two_pass(struct wlr_output *output, bool require_two_pass);
void wlr_kfx_output_set_dither(struct wlr_output *output, bool enable_dither);
void wlr_kfx_output_set_sdr_white_nits(struct wlr_output *output, float sdr_white_nits);
float wlr_kfx_output_get_sdr_white_nits(struct wlr_output *output);
void wlr_kfx_output_set_tonemap(struct wlr_output *output, bool enable_tonemap,
	float peak_luminance_nits);
bool wlr_kfx_output_get_tonemap(struct wlr_output *output, float *out_peak_luminance_nits);
float wlr_kfx_output_get_hdr_headroom(struct wlr_output *output);

bool wlr_kfx_scene_output_damage_intersects(struct wlr_scene_output *scene_output,
	const struct wlr_box *box);
void wlr_kfx_scene_output_damage_box(struct wlr_scene_output *scene_output,
	const struct wlr_box *box);

bool wlr_kfx_scene_output_render_below(struct wlr_scene_output *scene_output,
	struct wlr_scene_node *node, const struct wlr_box *box,
	struct wlr_kfx_target *dst, int downscale, struct wlr_box *out_src_box);

#endif
