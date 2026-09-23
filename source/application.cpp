#include "application.hpp"

#include <imgui.h>
#include <fstream>
#include <vector>
#include <iostream>

namespace application {

bool initialize() {
	return true;
}

void shutdown() {
	auto& context = graphics::internal::context;
	vkQueueWaitIdle(context.graphics_queue);
}

void update([[maybe_unused]] double time) {
	ImGui::ShowDemoWindow();
}

void render(const graphics::internal::FrameData& fd) {
	(void)fd;
}

} // namespace application

namespace {
auto& context = graphics::internal::context;

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

	if (vkCreateShaderModule(context.device, &shaderModuleCreateInfo, nullptr, &shaderModule) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan shader module\n";
		return nullptr;
	}
	
	return shaderModule;
}

bool createGraphicsPipelines(VkShaderModule vertShaderModule, VkShaderModule fragShaderModule) {
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

	VkPipelineLayout pipelineLayout;

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 0,
		.pushConstantRangeCount = 0
	};

	if (vkCreatePipelineLayout(context.device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan pipeline layout\n";
		return false;
	}

	VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo{
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = shaderStages,
		.pVertexInputState = &pipelineVertexInputStateCreateInfo,
		.pInputAssemblyState = &pipelineInputAssemblyStateCreateInfo,
		.pViewportState = &pipelineViewportStateCreateInfo,
		.pRasterizationState = &pipelineRasterizationStateCreateInfo,
		.pMultisampleState = &pipelineMultisampleStateCreateInfo,
		.pColorBlendState = &pipelineColorBlendStateCreateInfo,
		.pDynamicState = &pipelineDynamicStateCreateInfo,
		.layout = pipelineLayout,
		.renderPass = context.render_pass,
		.subpass = 0,
		.basePipelineHandle = VK_NULL_HANDLE
	};

	VkPipeline graphicPipeline;

	if (vkCreateGraphicsPipelines(
		context.device, VK_NULL_HANDLE, 1,
		&graphicsPipelineCreateInfo, nullptr, &graphicPipeline
	) != VK_SUCCESS) {
		std::cerr << "Failed to create Vulkan image view for depth buffer\n";
		return false;
	}
	return true;
}

}