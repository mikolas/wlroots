#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include <wlr/kfx/render.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>

#include "render/kfx.h"
#include "render/vulkan.h"
#include "util/matrix.h"

#define KFX_UBO_SLAB_SIZE (64 * 1024)
#define KFX_UBO_POOL_SETS 32

struct kfx_vk_ubo_pool {
	struct wl_list link;
	VkDescriptorPool pool;
	uint32_t free_sets;
};

struct kfx_vk_ubo_slab {
	struct wl_list link;
	VkBuffer buffer;
	VkDeviceMemory memory;
	void *cpu_mapping;
	VkDeviceSize size;
	VkDeviceSize used;
	VkDescriptorSet ds;
};

struct kfx_vk_pipeline_entry {
	struct wl_list link;
	struct wlr_vk_render_format_setup *setup;
	enum wlr_render_blend_mode blend_mode;
	VkPipeline vk;
};

struct wlr_kfx_shader {
	struct wlr_kfx_shader_layout layout;
	struct wlr_vk_renderer *renderer;
	VkShaderModule module;
	VkPipelineLayout pipe_layout;
	struct wl_list pipelines; // kfx_vk_pipeline_entry.link
	struct wl_list link;      // kfx_vk_state.shaders
};

struct kfx_vk_state {
	VkSampler sampler;
	VkDescriptorSetLayout sampler_ds_layouts[WLR_KFX_MAX_INPUTS + 1];
	VkDescriptorSetLayout ubo_ds_layout;
	VkPipelineLayout pipe_layouts[WLR_KFX_MAX_INPUTS + 1][2];
	VkDeviceSize ubo_step;
	struct wl_list ubo_pools;  // kfx_vk_ubo_pool.link
	struct wl_list slabs_free; // kfx_vk_ubo_slab.link
	struct wl_list shaders;    // wlr_kfx_shader.link
};

static VkDeviceSize align_up_size(VkDeviceSize v, VkDeviceSize align) {
	if (align <= 1) {
		return v;
	}
	return (v + align - 1) & ~(align - 1);
}

static void encode_proj_matrix(const float mat3[9], float mat4[4][4]) {
	float result[4][4] = {
		{ mat3[0], mat3[1], 0, mat3[2] },
		{ mat3[3], mat3[4], 0, mat3[5] },
		{ 0, 0, 1, 0 },
		{ 0, 0, 0, 1 },
	};
	memcpy(mat4, result, sizeof(result));
}

static struct kfx_vk_state *kfx_vk_ensure_state(struct wlr_vk_renderer *renderer) {
	if (renderer->kfx != NULL) {
		return renderer->kfx;
	}

	VkDevice dev = renderer->dev->dev;
	struct kfx_vk_state *kfx = calloc(1, sizeof(*kfx));
	if (kfx == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	wl_list_init(&kfx->ubo_pools);
	wl_list_init(&kfx->slabs_free);
	wl_list_init(&kfx->shaders);

	VkPhysicalDeviceProperties phdev_props = {0};
	vkGetPhysicalDeviceProperties(renderer->dev->phdev, &phdev_props);
	kfx->ubo_step = align_up_size(WLR_KFX_MAX_PARAMS_SIZE,
		phdev_props.limits.minUniformBufferOffsetAlignment);

	VkSamplerCreateInfo sampler_info = {
		.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
		.magFilter = VK_FILTER_LINEAR,
		.minFilter = VK_FILTER_LINEAR,
		.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
		.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
		.maxAnisotropy = 1.0f,
		.minLod = 0.0f,
		.maxLod = 0.25f,
	};
	VkResult res = vkCreateSampler(dev, &sampler_info, NULL, &kfx->sampler);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkCreateSampler", res);
		goto error;
	}

	for (uint32_t n = 0; n <= WLR_KFX_MAX_INPUTS; n++) {
		VkDescriptorSetLayoutBinding bindings[WLR_KFX_MAX_INPUTS];
		for (uint32_t i = 0; i < n; i++) {
			bindings[i] = (VkDescriptorSetLayoutBinding){
				.binding = i,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				.pImmutableSamplers = &kfx->sampler,
			};
		}
		VkDescriptorSetLayoutCreateInfo ds_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
			.bindingCount = n,
			.pBindings = n > 0 ? bindings : NULL,
		};
		res = vkCreateDescriptorSetLayout(dev, &ds_info, NULL,
			&kfx->sampler_ds_layouts[n]);
		if (res != VK_SUCCESS) {
			wlr_vk_error("vkCreateDescriptorSetLayout", res);
			goto error;
		}
	}

	VkDescriptorSetLayoutBinding ubo_binding = {
		.binding = 0,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
		.descriptorCount = 1,
		.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
	};
	VkDescriptorSetLayoutCreateInfo ubo_ds_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = &ubo_binding,
	};
	res = vkCreateDescriptorSetLayout(dev, &ubo_ds_info, NULL, &kfx->ubo_ds_layout);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkCreateDescriptorSetLayout", res);
		goto error;
	}

	VkPushConstantRange pc_range = {
		.offset = 0,
		.size = sizeof(struct wlr_vk_vert_pcr_data),
		.stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
	};

	for (uint32_t n = 0; n <= WLR_KFX_MAX_INPUTS; n++) {
		for (int has_params = 0; has_params < 2; has_params++) {
			VkDescriptorSetLayout set_layouts[2] = {
				kfx->sampler_ds_layouts[n],
				kfx->ubo_ds_layout,
			};
			VkPipelineLayoutCreateInfo pl_info = {
				.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
				.setLayoutCount = has_params ? 2 : 1,
				.pSetLayouts = set_layouts,
				.pushConstantRangeCount = 1,
				.pPushConstantRanges = &pc_range,
			};
			res = vkCreatePipelineLayout(dev, &pl_info, NULL,
				&kfx->pipe_layouts[n][has_params]);
			if (res != VK_SUCCESS) {
				wlr_vk_error("vkCreatePipelineLayout", res);
				goto error;
			}
		}
	}

	renderer->kfx = kfx;
	return kfx;

error:
	renderer->kfx = kfx;
	kfx_vk_renderer_finish(renderer);
	return NULL;
}

static void ubo_slab_destroy(struct wlr_vk_renderer *renderer,
		struct kfx_vk_ubo_slab *slab) {
	VkDevice dev = renderer->dev->dev;
	wl_list_remove(&slab->link);
	if (slab->cpu_mapping != NULL) {
		vkUnmapMemory(dev, slab->memory);
	}
	vkDestroyBuffer(dev, slab->buffer, NULL);
	vkFreeMemory(dev, slab->memory, NULL);
	free(slab);
}

static bool ubo_alloc_ds(struct wlr_vk_renderer *renderer,
		struct kfx_vk_state *kfx, VkDescriptorSet *out_ds) {
	VkDevice dev = renderer->dev->dev;

	struct kfx_vk_ubo_pool *pool;
	wl_list_for_each(pool, &kfx->ubo_pools, link) {
		if (pool->free_sets == 0) {
			continue;
		}
		VkDescriptorSetAllocateInfo alloc_info = {
			.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
			.descriptorPool = pool->pool,
			.descriptorSetCount = 1,
			.pSetLayouts = &kfx->ubo_ds_layout,
		};
		if (vkAllocateDescriptorSets(dev, &alloc_info, out_ds) == VK_SUCCESS) {
			pool->free_sets--;
			return true;
		}
	}

	pool = calloc(1, sizeof(*pool));
	if (pool == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return false;
	}

	VkDescriptorPoolSize pool_size = {
		.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
		.descriptorCount = KFX_UBO_POOL_SETS,
	};
	VkDescriptorPoolCreateInfo pool_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = KFX_UBO_POOL_SETS,
		.poolSizeCount = 1,
		.pPoolSizes = &pool_size,
	};
	VkResult res = vkCreateDescriptorPool(dev, &pool_info, NULL, &pool->pool);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkCreateDescriptorPool", res);
		free(pool);
		return false;
	}

	pool->free_sets = KFX_UBO_POOL_SETS;
	wl_list_insert(&kfx->ubo_pools, &pool->link);

	VkDescriptorSetAllocateInfo alloc_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = pool->pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &kfx->ubo_ds_layout,
	};
	res = vkAllocateDescriptorSets(dev, &alloc_info, out_ds);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkAllocateDescriptorSets", res);
		return false;
	}
	pool->free_sets--;
	return true;
}

static struct kfx_vk_ubo_slab *ubo_slab_create(struct wlr_vk_renderer *renderer,
		struct kfx_vk_state *kfx) {
	VkDevice dev = renderer->dev->dev;
	struct kfx_vk_ubo_slab *slab = calloc(1, sizeof(*slab));
	if (slab == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}
	wl_list_init(&slab->link);
	slab->size = KFX_UBO_SLAB_SIZE;
	if (slab->size < kfx->ubo_step) {
		slab->size = kfx->ubo_step;
	}

	VkBufferCreateInfo buf_info = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = slab->size,
		.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VkResult res = vkCreateBuffer(dev, &buf_info, NULL, &slab->buffer);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkCreateBuffer", res);
		goto error;
	}

	VkMemoryRequirements mem_reqs;
	vkGetBufferMemoryRequirements(dev, slab->buffer, &mem_reqs);

	int mem_type = vulkan_find_mem_type(renderer->dev,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		mem_reqs.memoryTypeBits);
	if (mem_type < 0) {
		wlr_log(WLR_ERROR, "Failed to find host-visible memory type for KFX UBO");
		goto error;
	}

	VkMemoryAllocateInfo mem_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_reqs.size,
		.memoryTypeIndex = (uint32_t)mem_type,
	};
	res = vkAllocateMemory(dev, &mem_info, NULL, &slab->memory);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkAllocateMemory", res);
		goto error;
	}

	res = vkBindBufferMemory(dev, slab->buffer, slab->memory, 0);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkBindBufferMemory", res);
		goto error;
	}

	res = vkMapMemory(dev, slab->memory, 0, VK_WHOLE_SIZE, 0, &slab->cpu_mapping);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkMapMemory", res);
		goto error;
	}

	if (!ubo_alloc_ds(renderer, kfx, &slab->ds)) {
		goto error;
	}

	VkDescriptorBufferInfo dbuf_info = {
		.buffer = slab->buffer,
		.offset = 0,
		.range = WLR_KFX_MAX_PARAMS_SIZE,
	};
	VkWriteDescriptorSet write = {
		.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
		.dstSet = slab->ds,
		.dstBinding = 0,
		.descriptorCount = 1,
		.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
		.pBufferInfo = &dbuf_info,
	};
	vkUpdateDescriptorSets(dev, 1, &write, 0, NULL);

	return slab;

error:
	ubo_slab_destroy(renderer, slab);
	return NULL;
}

static bool ubo_alloc_slot(struct wlr_vk_render_pass *pass,
		const void *params, size_t params_len,
		struct kfx_vk_ubo_slab **out_slab, uint32_t *out_offset) {
	struct wlr_vk_renderer *renderer = pass->renderer;
	struct kfx_vk_state *kfx = renderer->kfx;

	struct kfx_vk_ubo_slab *slab = NULL;
	struct kfx_vk_ubo_slab *cand;
	wl_list_for_each(cand, &pass->kfx_ubo_slabs, link) {
		if (cand->used + kfx->ubo_step <= cand->size) {
			slab = cand;
			break;
		}
	}

	if (slab == NULL && !wl_list_empty(&kfx->slabs_free)) {
		slab = wl_container_of(kfx->slabs_free.next, slab, link);
		wl_list_remove(&slab->link);
		slab->used = 0;
		wl_list_insert(&pass->kfx_ubo_slabs, &slab->link);
	}

	if (slab == NULL) {
		slab = ubo_slab_create(renderer, kfx);
		if (slab == NULL) {
			return false;
		}
		wl_list_insert(&pass->kfx_ubo_slabs, &slab->link);
	}

	VkDeviceSize offset = slab->used;
	uint8_t *dst = (uint8_t *)slab->cpu_mapping + offset;
	memset(dst, 0, WLR_KFX_MAX_PARAMS_SIZE);
	memcpy(dst, params, params_len);
	slab->used += kfx->ubo_step;

	*out_slab = slab;
	*out_offset = (uint32_t)offset;
	return true;
}

static VkPipeline kfx_shader_get_pipeline(struct wlr_kfx_shader *shader,
		struct wlr_vk_render_format_setup *setup,
		enum wlr_render_blend_mode blend_mode) {
	struct kfx_vk_pipeline_entry *entry;
	wl_list_for_each(entry, &shader->pipelines, link) {
		if (entry->setup == setup && entry->blend_mode == blend_mode) {
			return entry->vk;
		}
	}

	struct wlr_vk_renderer *renderer = shader->renderer;
	VkDevice dev = renderer->dev->dev;

	VkPipelineShaderStageCreateInfo stages[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = renderer->vert_module,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = shader->module,
			.pName = "main",
		},
	};

	VkPipelineInputAssemblyStateCreateInfo assembly = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN,
	};

	VkPipelineRasterizationStateCreateInfo rasterization = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};

	VkPipelineColorBlendAttachmentState blend_attachment = {
		.blendEnable = blend_mode == WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
		.srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.colorBlendOp = VK_BLEND_OP_ADD,
		.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
		.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
		.alphaBlendOp = VK_BLEND_OP_ADD,
		.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT |
			VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT |
			VK_COLOR_COMPONENT_A_BIT,
	};

	VkPipelineColorBlendStateCreateInfo blend = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &blend_attachment,
	};

	VkPipelineMultisampleStateCreateInfo multisample = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};

	VkPipelineViewportStateCreateInfo viewport = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1,
	};

	VkDynamicState dyn_states[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};
	VkPipelineDynamicStateCreateInfo dynamic = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.pDynamicStates = dyn_states,
		.dynamicStateCount = sizeof(dyn_states) / sizeof(dyn_states[0]),
	};

	VkPipelineVertexInputStateCreateInfo vertex = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	};

	VkGraphicsPipelineCreateInfo pinfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.layout = shader->pipe_layout,
		.renderPass = setup->render_pass,
		.subpass = 0,
		.stageCount = sizeof(stages) / sizeof(stages[0]),
		.pStages = stages,
		.pInputAssemblyState = &assembly,
		.pRasterizationState = &rasterization,
		.pColorBlendState = &blend,
		.pMultisampleState = &multisample,
		.pViewportState = &viewport,
		.pDynamicState = &dynamic,
		.pVertexInputState = &vertex,
	};

	entry = calloc(1, sizeof(*entry));
	if (entry == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return VK_NULL_HANDLE;
	}

	VkResult res = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pinfo,
		NULL, &entry->vk);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkCreateGraphicsPipelines", res);
		free(entry);
		return VK_NULL_HANDLE;
	}

	entry->setup = setup;
	entry->blend_mode = blend_mode;
	wl_list_insert(&shader->pipelines, &entry->link);
	return entry->vk;
}

static void kfx_shader_destroy_pipelines(struct wlr_kfx_shader *shader) {
	if (shader->renderer == NULL) {
		return;
	}

	VkDevice dev = shader->renderer->dev->dev;
	struct kfx_vk_pipeline_entry *entry, *tmp;
	wl_list_for_each_safe(entry, tmp, &shader->pipelines, link) {
		wl_list_remove(&entry->link);
		vkDestroyPipeline(dev, entry->vk, NULL);
		free(entry);
	}

	if (shader->module != VK_NULL_HANDLE) {
		vkDestroyShaderModule(dev, shader->module, NULL);
		shader->module = VK_NULL_HANDLE;
	}
	wl_list_remove(&shader->link);
	wl_list_init(&shader->link);
	shader->renderer = NULL;
}

struct wlr_kfx_shader *kfx_vk_shader_create(struct wlr_renderer *wlr_renderer,
		const uint32_t *spirv, size_t len,
		const struct wlr_kfx_shader_layout *layout) {
	if (wlr_renderer == NULL || spirv == NULL || len == 0 || (len % 4) != 0 ||
			layout == NULL) {
		wlr_log(WLR_ERROR, "Invalid arguments to wlr_kfx_shader_create");
		return NULL;
	}
	if (layout->n_inputs > WLR_KFX_MAX_INPUTS) {
		wlr_log(WLR_ERROR, "wlr_kfx shader n_inputs (%zu) exceeds max %d",
			layout->n_inputs, WLR_KFX_MAX_INPUTS);
		return NULL;
	}
	if (layout->params_size > WLR_KFX_MAX_PARAMS_SIZE) {
		wlr_log(WLR_ERROR, "wlr_kfx shader params_size (%zu) exceeds max %d",
			layout->params_size, WLR_KFX_MAX_PARAMS_SIZE);
		return NULL;
	}

	struct wlr_vk_renderer *renderer = vulkan_get_renderer(wlr_renderer);
	struct kfx_vk_state *kfx = kfx_vk_ensure_state(renderer);
	if (kfx == NULL) {
		return NULL;
	}

	struct wlr_kfx_shader *shader = calloc(1, sizeof(*shader));
	if (shader == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	wl_list_init(&shader->pipelines);
	wl_list_init(&shader->link);
	shader->layout = *layout;
	shader->renderer = renderer;
	shader->pipe_layout = kfx->pipe_layouts[layout->n_inputs][layout->params_size > 0 ? 1 : 0];

	VkShaderModuleCreateInfo sinfo = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = len,
		.pCode = spirv,
	};
	VkResult res = vkCreateShaderModule(renderer->dev->dev, &sinfo, NULL, &shader->module);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkCreateShaderModule", res);
		free(shader);
		return NULL;
	}

	wl_list_insert(&kfx->shaders, &shader->link);
	return shader;
}

void kfx_vk_shader_destroy(struct wlr_kfx_shader *shader) {
	if (shader == NULL) {
		return;
	}

	if (shader->renderer != NULL) {
		struct wlr_vk_renderer *renderer = shader->renderer;
		if (!wl_list_empty(&shader->pipelines) && renderer->timeline_point > 0) {
			uint64_t current_point = 0;
			VkResult res = renderer->dev->api.vkGetSemaphoreCounterValueKHR(
				renderer->dev->dev, renderer->timeline_semaphore, &current_point);
			if (res == VK_SUCCESS && current_point < renderer->timeline_point) {
				VkSemaphoreWaitInfoKHR wait_info = {
					.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO_KHR,
					.semaphoreCount = 1,
					.pSemaphores = &renderer->timeline_semaphore,
					.pValues = &renderer->timeline_point,
				};
				renderer->dev->api.vkWaitSemaphoresKHR(renderer->dev->dev,
					&wait_info, UINT64_MAX);
			}
		}
		kfx_shader_destroy_pipelines(shader);
	}

	free(shader);
}

void kfx_vk_render_pass_add_draw(struct wlr_render_pass *wlr_pass,
		const struct wlr_kfx_draw_options *options) {
	if (!vulkan_render_pass_is_vk(wlr_pass)) {
		return;
	}

	struct wlr_vk_render_pass *pass = vulkan_get_render_pass(wlr_pass);
	if (pass->failed || options == NULL || options->shader == NULL) {
		pass->failed = true;
		return;
	}

	struct wlr_kfx_shader *shader = options->shader;
	if (shader->renderer != pass->renderer) {
		wlr_log(WLR_ERROR, "wlr_kfx_shader does not belong to pass renderer");
		pass->failed = true;
		return;
	}
	if (shader->layout.n_inputs > 0) {
		wlr_log(WLR_ERROR, "wlr_kfx_target inputs are not implemented yet");
		pass->failed = true;
		return;
	}
	if (options->params_len != shader->layout.params_size ||
			(options->params_len > 0 && options->params == NULL)) {
		wlr_log(WLR_ERROR, "wlr_kfx params length mismatch (%zu vs shader %zu)",
			options->params_len, shader->layout.params_size);
		pass->failed = true;
		return;
	}
	if (options->blend_mode != WLR_RENDER_BLEND_MODE_PREMULTIPLIED &&
			options->blend_mode != WLR_RENDER_BLEND_MODE_NONE) {
		wlr_log(WLR_ERROR, "Unsupported wlr_kfx blend mode %d", options->blend_mode);
		pass->failed = true;
		return;
	}

	if (options->dst_box.width <= 0 || options->dst_box.height <= 0) {
		return;
	}

	pixman_region32_t clip;
	if (options->clip != NULL) {
		pixman_region32_init(&clip);
		pixman_region32_copy(&clip, options->clip);
	} else {
		struct wlr_buffer *buf = pass->render_buffer->wlr_buffer;
		pixman_region32_init_rect(&clip, 0, 0, buf->width, buf->height);
	}

	int clip_rects_len = 0;
	const pixman_box32_t *clip_rects = pixman_region32_rectangles(&clip, &clip_rects_len);
	bool any_visible = false;
	for (int i = 0; i < clip_rects_len; i++) {
		struct wlr_box clip_box = {
			.x = clip_rects[i].x1,
			.y = clip_rects[i].y1,
			.width = clip_rects[i].x2 - clip_rects[i].x1,
			.height = clip_rects[i].y2 - clip_rects[i].y1,
		};
		struct wlr_box intersection;
		if (!wlr_box_intersection(&intersection, &options->dst_box, &clip_box)) {
			continue;
		}
		vulkan_render_pass_mark_box_updated(pass, &intersection);
		any_visible = true;
	}

	if (!any_visible) {
		pixman_region32_fini(&clip);
		return;
	}

	VkPipeline pipe = kfx_shader_get_pipeline(shader, pass->render_setup,
		options->blend_mode);
	if (pipe == VK_NULL_HANDLE) {
		pixman_region32_fini(&clip);
		pass->failed = true;
		return;
	}

	struct kfx_vk_ubo_slab *slab = NULL;
	uint32_t dyn_offset = 0;
	if (shader->layout.params_size > 0) {
		if (!ubo_alloc_slot(pass, options->params, options->params_len,
				&slab, &dyn_offset)) {
			pixman_region32_fini(&clip);
			pass->failed = true;
			return;
		}
	}

	float proj[9], matrix[9];
	wlr_matrix_identity(proj);
	wlr_matrix_project_box(matrix, &options->dst_box, WL_OUTPUT_TRANSFORM_NORMAL, proj);
	wlr_matrix_multiply(matrix, pass->projection, matrix);

	struct wlr_vk_vert_pcr_data vert_pcr_data = {
		.uv_off = { 0.0f, 0.0f },
		.uv_size = { 1.0f, 1.0f },
	};
	encode_proj_matrix(matrix, vert_pcr_data.mat4);

	VkCommandBuffer cb = pass->command_buffer->vk;
	if (pass->bound_pipeline != pipe) {
		vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
		pass->bound_pipeline = pipe;
	}

	vkCmdPushConstants(cb, shader->pipe_layout, VK_SHADER_STAGE_VERTEX_BIT,
		0, sizeof(vert_pcr_data), &vert_pcr_data);

	if (slab != NULL) {
		vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
			shader->pipe_layout, 1, 1, &slab->ds, 1, &dyn_offset);
	}

	for (int i = 0; i < clip_rects_len; i++) {
		struct wlr_box clip_box = {
			.x = clip_rects[i].x1,
			.y = clip_rects[i].y1,
			.width = clip_rects[i].x2 - clip_rects[i].x1,
			.height = clip_rects[i].y2 - clip_rects[i].y1,
		};
		struct wlr_box intersection;
		if (!wlr_box_intersection(&intersection, &options->dst_box, &clip_box)) {
			continue;
		}
		VkRect2D rect = {
			.offset = { .x = intersection.x, .y = intersection.y },
			.extent = {
				.width = (uint32_t)intersection.width,
				.height = (uint32_t)intersection.height,
			},
		};
		vkCmdSetScissor(cb, 0, 1, &rect);
		vkCmdDraw(cb, 4, 1, 0, 0);
	}

	pixman_region32_fini(&clip);
}

void kfx_vk_pass_submit(struct wlr_vk_render_pass *pass) {
	if (wl_list_empty(&pass->kfx_ubo_slabs)) {
		return;
	}
	wl_list_insert_list(&pass->command_buffer->kfx_ubo_slabs, &pass->kfx_ubo_slabs);
	wl_list_init(&pass->kfx_ubo_slabs);
}

void kfx_vk_pass_cleanup(struct wlr_vk_render_pass *pass) {
	if (wl_list_empty(&pass->kfx_ubo_slabs) || pass->renderer->kfx == NULL) {
		return;
	}
	struct kfx_vk_ubo_slab *slab, *tmp;
	wl_list_for_each_safe(slab, tmp, &pass->kfx_ubo_slabs, link) {
		slab->used = 0;
		wl_list_remove(&slab->link);
		wl_list_insert(&pass->renderer->kfx->slabs_free, &slab->link);
	}
}

void kfx_vk_cb_release(struct wlr_vk_command_buffer *cb,
		struct wlr_vk_renderer *renderer) {
	if (wl_list_empty(&cb->kfx_ubo_slabs) || renderer->kfx == NULL) {
		return;
	}
	struct kfx_vk_ubo_slab *slab, *tmp;
	wl_list_for_each_safe(slab, tmp, &cb->kfx_ubo_slabs, link) {
		slab->used = 0;
		wl_list_remove(&slab->link);
		wl_list_insert(&renderer->kfx->slabs_free, &slab->link);
	}
}

void kfx_vk_renderer_finish(struct wlr_vk_renderer *renderer) {
	struct kfx_vk_state *kfx = renderer->kfx;
	if (kfx == NULL) {
		return;
	}

	VkDevice dev = renderer->dev->dev;

	struct wlr_kfx_shader *shader, *shader_tmp;
	wl_list_for_each_safe(shader, shader_tmp, &kfx->shaders, link) {
		wlr_log(WLR_DEBUG, "kfx shader destroyed implicitly during renderer teardown");
		kfx_shader_destroy_pipelines(shader);
	}

	struct kfx_vk_ubo_slab *slab, *slab_tmp;
	wl_list_for_each_safe(slab, slab_tmp, &kfx->slabs_free, link) {
		ubo_slab_destroy(renderer, slab);
	}

	struct kfx_vk_ubo_pool *pool, *pool_tmp;
	wl_list_for_each_safe(pool, pool_tmp, &kfx->ubo_pools, link) {
		wl_list_remove(&pool->link);
		vkDestroyDescriptorPool(dev, pool->pool, NULL);
		free(pool);
	}

	for (uint32_t n = 0; n <= WLR_KFX_MAX_INPUTS; n++) {
		for (int has_params = 0; has_params < 2; has_params++) {
			vkDestroyPipelineLayout(dev, kfx->pipe_layouts[n][has_params], NULL);
		}
		vkDestroyDescriptorSetLayout(dev, kfx->sampler_ds_layouts[n], NULL);
	}
	vkDestroyDescriptorSetLayout(dev, kfx->ubo_ds_layout, NULL);
	vkDestroySampler(dev, kfx->sampler, NULL);

	free(kfx);
	renderer->kfx = NULL;
}
