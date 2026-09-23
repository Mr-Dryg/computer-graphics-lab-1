#include "application.hpp"
#include "vulkan/vulkan_core.h"

#include <imgui.h>
#include <fstream>
#include <vector>
#include <iostream>

auto& context = graphics::internal::context;
VkPipeline graphicPipeline;
VkPipelineLayout pipelineLayout;

std::vector<char> readFile(const std::string& path) {
	std::ifstream file(path, std::ios::ate | std::ios::binary);
	size_t fileSize = static_cast<size_t>(file.tellg());
	std::vector<char> buffer(fileSize);
	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();
	return buffer;
}

VkShaderModule createShaderModule(const std::vector<char>& shaderCode) {
	VkShaderModuleCreateInfo shaderModuleCreateInfo{
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = shaderCode.size(),
		.pCode = reinterpret_cast<const uint32_t*>(shaderCode.data())
	};
	VkShaderModule shaderModule;

	if (vkCreateShaderModule(
		context.device, &shaderModuleCreateInfo,
		nullptr, &shaderModule
	) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan shader module\n";
		return nullptr;
	}
	
	return shaderModule;
}

VkPipeline createGraphicPipeline(VkShaderModule vertShaderModule, VkShaderModule fragShaderModule) {
	VkPipelineShaderStageCreateInfo vertPipelineShaderStageCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.module = vertShaderModule,
		.pName = "main"
	};

	VkPipelineShaderStageCreateInfo fragPipelineShaderStageCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.module = fragShaderModule,
		.pName = "main"
	};

	VkPipelineShaderStageCreateInfo shaderStages[] = {
		vertPipelineShaderStageCreateInfo,
		fragPipelineShaderStageCreateInfo
	};

	VkPipelineVertexInputStateCreateInfo pipelineVertexInputStateCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 0,
		.vertexAttributeDescriptionCount = 0
	};

	VkPipelineInputAssemblyStateCreateInfo pipelineInputAssemblyStateCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE
	};

	VkPipelineViewportStateCreateInfo pipelineViewportStateCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.scissorCount = 1
	};

	VkPipelineRasterizationStateCreateInfo pipelineRasterizationStateCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_BACK_BIT,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
		.lineWidth = 1.0
	};

	VkPipelineMultisampleStateCreateInfo pipelineMultisampleStateCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		.sampleShadingEnable = VK_FALSE
	};

	VkPipelineColorBlendAttachmentState pipelineColorBlendAttachmentState{
		.blendEnable = VK_FALSE,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
	};

	VkPipelineColorBlendStateCreateInfo pipelineColorBlendStateCreateInfo {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		.attachmentCount = 1,
		.pAttachments = &pipelineColorBlendAttachmentState
	};

	std::vector<VkDynamicState> dynamicState = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	};

	VkPipelineDynamicStateCreateInfo pipelineDynamicStateCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
		.dynamicStateCount = static_cast<uint32_t>(dynamicState.size()),
		.pDynamicStates = dynamicState.data()
	};

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 0,
		.pushConstantRangeCount = 0
	};

	if (vkCreatePipelineLayout(
		context.device, &pipelineLayoutCreateInfo,
		nullptr, &pipelineLayout
	) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan pipeline layout\n";
		return nullptr;
	}

	VkPipelineDepthStencilStateCreateInfo pipelineDepthStencilStateCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = VK_TRUE,
		.depthWriteEnable = VK_TRUE,
		.depthCompareOp = VK_COMPARE_OP_LESS,
	};

	VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = shaderStages,
		.pVertexInputState = &pipelineVertexInputStateCreateInfo,
		.pInputAssemblyState = &pipelineInputAssemblyStateCreateInfo,
		.pViewportState = &pipelineViewportStateCreateInfo,
		.pRasterizationState = &pipelineRasterizationStateCreateInfo,
		.pMultisampleState = &pipelineMultisampleStateCreateInfo,
		.pDepthStencilState = &pipelineDepthStencilStateCreateInfo,
		.pColorBlendState = &pipelineColorBlendStateCreateInfo,
		.pDynamicState = &pipelineDynamicStateCreateInfo,
		.layout = pipelineLayout,
		.renderPass = context.render_pass,
		.subpass = 0,
		.basePipelineHandle = VK_NULL_HANDLE
	};

	

	if (vkCreateGraphicsPipelines(
		context.device, VK_NULL_HANDLE, 1,
		&graphicsPipelineCreateInfo, nullptr, &graphicPipeline
	) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan graphic pipeline\n";
		return nullptr;
	}
	return graphicPipeline;
}

namespace application {

bool initialize() {
	auto vertShaderCode = readFile("shaders/pyramid.vert.spv");
	auto fragShaderCode = readFile("shaders/pyramid.frag.spv");

	auto vertShaderModule = createShaderModule(vertShaderCode);
	if (vertShaderModule == nullptr) {
		vkDestroyShaderModule(context.device, vertShaderModule, nullptr);
		return false;
	}

	auto fragShaderModule = createShaderModule(fragShaderCode);
	if (fragShaderModule == nullptr) {
		vkDestroyShaderModule(context.device, vertShaderModule, nullptr);
		vkDestroyShaderModule(context.device, fragShaderModule, nullptr);
		return false;
	}

	graphicPipeline = createGraphicPipeline(vertShaderModule, fragShaderModule);
	vkDestroyShaderModule(context.device, vertShaderModule, nullptr);
	vkDestroyShaderModule(context.device, fragShaderModule, nullptr);
	if (graphicPipeline == nullptr) {
		return false;
	}
	return true;
}

void shutdown() {
	vkQueueWaitIdle(context.graphics_queue);
	vkDestroyPipeline(context.device, graphicPipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipelineLayout, nullptr);
}

void update([[maybe_unused]] double time) {
	ImGui::ShowDemoWindow();
}

void render(const graphics::internal::FrameData& fd) {
    vkResetCommandBuffer(fd.command_buffer, 0);

    const VkCommandBufferBeginInfo begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(fd.command_buffer, &begin);

    const VkClearValue clears[2] = {
        { .color = { { 0.1f, 0.1f, 0.1f, 1.0f } } },
        { .depthStencil = { .depth = 1.0f, .stencil = 0 } },
    };

    const VkRenderPassBeginInfo rp = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = context.render_pass,
        .framebuffer = fd.framebuffer,
        .renderArea = { .extent = context.swapchain_extent },
        .clearValueCount = 2,
        .pClearValues = clears,
    };
    vkCmdBeginRenderPass(fd.command_buffer, &rp, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(fd.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicPipeline);

    const VkViewport viewport = {
        0.0f, 0.0f,
        float(context.swapchain_extent.width), float(context.swapchain_extent.height),
        0.0f, 1.0f,
    };
    const VkRect2D scissor = { .offset = {0, 0}, .extent = context.swapchain_extent };
    vkCmdSetViewport(fd.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(fd.command_buffer, 0, 1, &scissor);

    vkCmdDraw(fd.command_buffer, 3, 1, 0, 0);

    vkCmdEndRenderPass(fd.command_buffer);
    vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application