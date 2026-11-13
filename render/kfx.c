#include <stddef.h>
#include <wlr/config.h>
#include <wlr/kfx/render.h>
#include <wlr/render/wlr_renderer.h>

#if WLR_HAS_VULKAN_RENDERER
#include <wlr/render/vulkan.h>
#include "render/kfx.h"
#endif

bool wlr_kfx_is_supported(struct wlr_renderer *renderer) {
#if WLR_HAS_VULKAN_RENDERER
	return renderer != NULL && wlr_renderer_is_vk(renderer);
#else
	(void)renderer;
	return false;
#endif
}

struct wlr_kfx_shader *wlr_kfx_shader_create(struct wlr_renderer *renderer,
		const uint32_t *spirv, size_t len,
		const struct wlr_kfx_shader_layout *layout) {
#if WLR_HAS_VULKAN_RENDERER
	if (!wlr_kfx_is_supported(renderer)) {
		return NULL;
	}
	return kfx_vk_shader_create(renderer, spirv, len, layout);
#else
	(void)renderer;
	(void)spirv;
	(void)len;
	(void)layout;
	return NULL;
#endif
}

void wlr_kfx_shader_destroy(struct wlr_kfx_shader *shader) {
#if WLR_HAS_VULKAN_RENDERER
	if (shader != NULL) {
		kfx_vk_shader_destroy(shader);
	}
#else
	(void)shader;
#endif
}

void wlr_kfx_render_pass_add_draw(struct wlr_render_pass *pass,
		const struct wlr_kfx_draw_options *options) {
#if WLR_HAS_VULKAN_RENDERER
	if (pass != NULL && options != NULL) {
		kfx_vk_render_pass_add_draw(pass, options);
	}
#else
	(void)pass;
	(void)options;
#endif
}

struct wlr_render_pass *wlr_kfx_renderer_begin_buffer_pass(
		struct wlr_renderer *renderer, struct wlr_buffer *buffer,
		const struct wlr_buffer_pass_options *options, bool require_two_pass) {
#if WLR_HAS_VULKAN_RENDERER
	if (require_two_pass && renderer != NULL && wlr_renderer_is_vk(renderer)) {
		return vulkan_begin_buffer_pass_with_flags(renderer, buffer, options, true);
	}
#else
	(void)require_two_pass;
#endif
	return wlr_renderer_begin_buffer_pass(renderer, buffer, options);
}
