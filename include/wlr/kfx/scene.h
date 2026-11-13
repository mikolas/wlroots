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

#endif
