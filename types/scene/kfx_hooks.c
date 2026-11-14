#include <stdlib.h>
#include <wlr/kfx/scene.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_scene.h>

#include "types/wlr_scene.h"

struct kfx_output_state {
	struct wlr_addon addon;
	bool require_two_pass;
	bool dither;
};

static const int kfx_hook_addon_owner = 0;
static const int kfx_output_addon_owner = 0;

static void kfx_hook_addon_destroy(struct wlr_addon *addon) {
	struct wlr_kfx_node_hook *hook = wl_container_of(addon, hook, addon);
	wlr_addon_finish(&hook->addon);
	hook->active = false;
	hook->node = NULL;
}

static const struct wlr_addon_interface kfx_hook_addon_impl = {
	.name = "wlr_kfx_node_hook",
	.destroy = kfx_hook_addon_destroy,
};

static void kfx_output_addon_destroy(struct wlr_addon *addon) {
	struct kfx_output_state *state = wl_container_of(addon, state, addon);
	wlr_addon_finish(&state->addon);
	free(state);
}

static const struct wlr_addon_interface kfx_output_addon_impl = {
	.name = "wlr_kfx_output_two_pass",
	.destroy = kfx_output_addon_destroy,
};

bool wlr_kfx_node_hook_init(struct wlr_kfx_node_hook *hook,
		struct wlr_scene_node *leaf, enum wlr_kfx_hook_stage stage,
		const struct wlr_kfx_node_hook_impl *impl) {
	if (hook == NULL || leaf == NULL || impl == NULL || impl->render == NULL) {
		return false;
	}
	if (leaf->type != WLR_SCENE_NODE_RECT && leaf->type != WLR_SCENE_NODE_BUFFER) {
		return false;
	}
	if (stage != WLR_KFX_BEFORE && stage != WLR_KFX_INSTEAD && stage != WLR_KFX_AFTER) {
		return false;
	}
	if (wlr_addon_find(&leaf->addons, &kfx_hook_addon_owner, &kfx_hook_addon_impl) != NULL) {
		return false;
	}

	hook->stage = stage;
	hook->impl = impl;
	hook->node = leaf;
	hook->active = true;
	wlr_addon_init(&hook->addon, &leaf->addons, &kfx_hook_addon_owner, &kfx_hook_addon_impl);
	return true;
}

void wlr_kfx_node_hook_finish(struct wlr_kfx_node_hook *hook) {
	if (hook == NULL || !hook->active) {
		return;
	}
	hook->active = false;
	hook->node = NULL;
	wlr_addon_finish(&hook->addon);
}

struct wlr_kfx_node_hook *kfx_scene_node_get_hook(struct wlr_scene_node *node) {
	if (node == NULL || (node->type != WLR_SCENE_NODE_RECT &&
			node->type != WLR_SCENE_NODE_BUFFER)) {
		return NULL;
	}
	struct wlr_addon *addon = wlr_addon_find(&node->addons,
		&kfx_hook_addon_owner, &kfx_hook_addon_impl);
	if (addon == NULL) {
		return NULL;
	}
	struct wlr_kfx_node_hook *hook = wl_container_of(addon, hook, addon);
	return hook->active ? hook : NULL;
}

static struct kfx_output_state *kfx_output_get_or_create_state(
		struct wlr_output *output, bool create) {
	if (output == NULL) {
		return NULL;
	}
	struct wlr_addon *addon = wlr_addon_find(&output->addons,
		&kfx_output_addon_owner, &kfx_output_addon_impl);
	if (addon != NULL) {
		struct kfx_output_state *state = wl_container_of(addon, state, addon);
		return state;
	}
	if (!create) {
		return NULL;
	}
	struct kfx_output_state *state = calloc(1, sizeof(*state));
	if (state == NULL) {
		return NULL;
	}
	wlr_addon_init(&state->addon, &output->addons,
		&kfx_output_addon_owner, &kfx_output_addon_impl);
	return state;
}

static void kfx_output_cleanup_state_if_idle(struct kfx_output_state *state) {
	if (state != NULL && !state->require_two_pass && !state->dither) {
		kfx_output_addon_destroy(&state->addon);
	}
}

void wlr_kfx_output_set_two_pass(struct wlr_output *output, bool require_two_pass) {
	struct kfx_output_state *state =
		kfx_output_get_or_create_state(output, require_two_pass);
	if (state == NULL) {
		return;
	}
	state->require_two_pass = require_two_pass;
	kfx_output_cleanup_state_if_idle(state);
}

void wlr_kfx_output_set_dither(struct wlr_output *output, bool enable_dither) {
	struct kfx_output_state *state =
		kfx_output_get_or_create_state(output, enable_dither);
	if (state == NULL) {
		return;
	}
	state->dither = enable_dither;
	kfx_output_cleanup_state_if_idle(state);
}

bool kfx_output_requires_two_pass(struct wlr_output *output) {
	struct kfx_output_state *state = kfx_output_get_or_create_state(output, false);
	return state != NULL && (state->require_two_pass || state->dither);
}

bool kfx_output_get_dither(struct wlr_output *output) {
	struct kfx_output_state *state = kfx_output_get_or_create_state(output, false);
	return state != NULL && state->dither;
}
