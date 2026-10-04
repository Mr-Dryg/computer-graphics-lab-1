#include "application.hpp"
#include "vulkan/vulkan_core.h"

#include <imgui.h>
#include <fstream>
#include <vector>
#include <iostream>
#include <cstring>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

auto& context = graphics::internal::context;
VkPipeline graphicPipeline;
VkPipelineLayout pipelineLayout;

VkBuffer vertexBuffer;
VmaAllocation vertexBufferAllocation;
VkBuffer indexBuffer;
VmaAllocation indexBufferAllocation;

struct Vertex {
	float position[3];
};

constexpr glm::vec3 color_front = glm::vec3(0.90f, 0.32f, 0.26f);  // коралловый
constexpr glm::vec3 color_right = glm::vec3(0.95f, 0.62f, 0.20f);  // янтарный
constexpr glm::vec3 color_back = glm::vec3(0.36f, 0.72f, 0.42f);   // зелёный
constexpr glm::vec3 color_left = glm::vec3(0.24f, 0.68f, 0.76f);   // бирюзовый
constexpr glm::vec3 color_bottom = glm::vec3(0.45f, 0.40f, 0.58f); // тёмно-фиолетовый

const std::vector<Vertex> vertices = {
	{{ 0.5f, -0.5f,  0.5f}},   // 0
	{{ 0.5f, -0.5f, -0.5f}},   // 1
	{{-0.5f, -0.5f, -0.5f}},   // 2
	{{-0.5f, -0.5f,  0.5f}},   // 3
	{{ 0.0f,  0.5f,  0.0f}},   // 4 — вершина
};

const VkVertexInputBindingDescription bindingDescription = {
	.binding = 0,
	.stride = sizeof(Vertex),
	.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
};

const VkVertexInputAttributeDescription attributeDescriptions[] = {
	{
		.location = 0,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32_SFLOAT,
		.offset = offsetof(Vertex, position)
	},
};

const std::vector<uint16_t> indices = {
	4, 0, 3,   // передняя грань
	4, 1, 0,   // правая грань
	4, 2, 1,   // задняя грань
	4, 3, 2,   // левая грань
	1, 2, 0,   // дно, первая половина
	2, 3, 0,   // дно, вторая половина
};

struct Transform {
	glm::mat4 modelViewProjection;
	glm::vec3 color;
};
static_assert(sizeof(Transform) == 76, "раскладка push constants разошлась с шейдером");

constexpr glm::vec3 faceColors[6] = {
	color_front,   // передняя
	color_right,   // правая
	color_back,    // задняя
	color_left,    // левая
	color_bottom,  // дно, 1-я половина
	color_bottom,  // дно, 2-я половина
};

// Начальные значения трансформаций.
constexpr glm::vec3 initialPosition{0.0f, 0.0f, 0.0f};
constexpr glm::vec3 initialRotationDegrees{20.0f, 0.0f, 0.0f};
constexpr glm::vec3 initialScale{1.0f, 1.0f, 1.0f};

// Всё, чем можно управлять из интерфейса
struct SceneState {
	// Задание 1: проекция
	bool perspective = true;
	float fovDegrees = 45.0f;  // угол обзора по вертикали, градусы

	// Задание 2: трансформации.
	glm::vec3 position = initialPosition;
	glm::vec3 rotationDegrees = initialRotationDegrees;  // наклон по X обязателен
	glm::vec3 scale = initialScale;
};

SceneState state;

namespace application {

namespace {

GLFWwindow* window = nullptr;

bool mouseButtonHeld = false;
double lastMouseX = 0.0;
double lastMouseY = 0.0;

// При 90° ось поворота совпадает с направлением взгляда и матрица
// вырождается — объект переворачивается вверх ногами.
constexpr float maxPitchDegrees = 89.0f;

} // namespace

void attachWindow(GLFWwindow* const glfwWindow) {
	window = glfwWindow;

	glfwSetMouseButtonCallback(window, [](GLFWwindow*, int button, int action, int) {
		application::onMouseButton(button, action);
	});

	glfwSetCursorPosCallback(window, [](GLFWwindow*, double xpos, double ypos) {
		application::onCursorPos(xpos, ypos);
	});
}

void onMouseButton(int button, int action) {
	if (button != GLFW_MOUSE_BUTTON_LEFT) {
		return;
	}

	mouseButtonHeld = (action == GLFW_PRESS);

	if (mouseButtonHeld && window != nullptr) {
		glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
	}
}

void onCursorPos(double xpos, double ypos) {
	if (!mouseButtonHeld) {
		return;
	}

	if (ImGui::GetIO().WantCaptureMouse) {
		lastMouseX = xpos;
		lastMouseY = ypos;
		return;
	}

	const double dx = xpos - lastMouseX;
	const double dy = ypos - lastMouseY;

	lastMouseX = xpos;
	lastMouseY = ypos;

	constexpr double mouseSensitivity = 0.3;
	state.rotationDegrees.y += static_cast<float>(dx * mouseSensitivity);
	state.rotationDegrees.x += static_cast<float>(dy * mouseSensitivity);

	state.rotationDegrees.x = glm::clamp(
		state.rotationDegrees.x, -maxPitchDegrees, maxPitchDegrees
	);
}

} // namespace application

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
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &bindingDescription,
		.vertexAttributeDescriptionCount = 1,
		.pVertexAttributeDescriptions = attributeDescriptions
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

	VkPushConstantRange pushConstantRange{};
	pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
	pushConstantRange.offset = 0;
	pushConstantRange.size = sizeof(Transform);

	VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo{
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 0,
		.pSetLayouts = nullptr,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &pushConstantRange
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

bool createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      const void* data, VkBuffer* buffer,
                      VmaAllocation* allocation) {
	const VkBufferCreateInfo bufferInfo{
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = usage,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE
	};

	const VmaAllocationCreateInfo allocInfo{
		.usage = VMA_MEMORY_USAGE_CPU_TO_GPU
	};

	if (vmaCreateBuffer(context.allocator, &bufferInfo, &allocInfo,
						buffer, allocation, nullptr) != VK_SUCCESS) {
		std::cerr << "Failed to allocate buffer\n";
		return false;
	}

	void* mapped = nullptr;
	if (vmaMapMemory(context.allocator, *allocation, &mapped) != VK_SUCCESS) {
		std::cerr << "Failed to map buffer memory\n";
		return false;
	}

	if (data != nullptr) {
		std::memcpy(mapped, data, static_cast<size_t>(size));
	}

	vmaUnmapMemory(context.allocator, *allocation);

	return true;
}

bool createVertexBuffer() {
	return createHostBuffer(
		vertices.size() * sizeof(Vertex),
		VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		vertices.data(),
		&vertexBuffer, &vertexBufferAllocation
	);
}

bool createIndexBuffer() {
	return createHostBuffer(
		indices.size() * sizeof(uint16_t),
		VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		indices.data(),
		&indexBuffer, &indexBufferAllocation
	);
}

namespace application {

bool initialize() {
	if (!createVertexBuffer()) {
		return false;
	}

	if (!createIndexBuffer()) {
		return false;
	}

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
	vmaDestroyBuffer(context.allocator, vertexBuffer, vertexBufferAllocation);
	vmaDestroyBuffer(context.allocator, indexBuffer, indexBufferAllocation);
	vkDestroyPipeline(context.device, graphicPipeline, nullptr);
	vkDestroyPipelineLayout(context.device, pipelineLayout, nullptr);
}

static void drawControlPanel() {
	ImGui::Begin("Controls");

	// Задание 1: переключение проекции
	if (ImGui::Checkbox("Перспективная проекция", &state.perspective)) {
	}

	// Ползунок только для активной проекции: FOV в ортографическом режиме ни на что не влияет.
	if (state.perspective) {
		ImGui::SliderFloat("Угол обзора (FOV)", &state.fovDegrees, 15.0f, 90.0f, "%.0f deg");
	}

	ImGui::Separator();

	// Задание 2: трансформации.
	ImGui::TextUnformatted("Позиция");
	ImGui::PushID("position");
	ImGui::DragFloat("X", &state.position.x, 0.05f, -5.0f, 5.0f, "%.2f");
	ImGui::DragFloat("Y", &state.position.y, 0.05f, -5.0f, 5.0f, "%.2f");
	ImGui::DragFloat("Z", &state.position.z, 0.05f, -5.0f, 5.0f, "%.2f");
	if (ImGui::Button("Сбросить")) {
		state.position = initialPosition;
	}
	ImGui::PopID();

	ImGui::TextUnformatted("Поворот (градусы)");
	ImGui::PushID("rotation");
	ImGui::DragFloat("X", &state.rotationDegrees.x, 0.5f, -360.0f, 360.0f, "%.0f");
	ImGui::DragFloat("Y", &state.rotationDegrees.y, 0.5f, -360.0f, 360.0f, "%.0f");
	ImGui::DragFloat("Z", &state.rotationDegrees.z, 0.5f, -360.0f, 360.0f, "%.0f");
	if (ImGui::Button("Сбросить")) {
		state.rotationDegrees = initialRotationDegrees;
	}
	ImGui::PopID();

	ImGui::TextUnformatted("Растяжение");
	ImGui::PushID("scale");
	ImGui::DragFloat("X", &state.scale.x, 0.05f, 0.1f, 5.0f, "%.2f");
	ImGui::DragFloat("Y", &state.scale.y, 0.05f, 0.1f, 5.0f, "%.2f");
	ImGui::DragFloat("Z", &state.scale.z, 0.05f, 0.1f, 5.0f, "%.2f");
	if (ImGui::Button("Сбросить")) {
		state.scale = initialScale;
	}
	ImGui::PopID();

	ImGui::End();
}

double currentTime = 0.0;

void update(double time) {
	currentTime = time;
	drawControlPanel();
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

    const VkDeviceSize bufferOffset = 0;
    vkCmdBindVertexBuffers(fd.command_buffer, 0, 1, &vertexBuffer, &bufferOffset);
    vkCmdBindIndexBuffer(fd.command_buffer, indexBuffer, 0, VK_INDEX_TYPE_UINT16);

    // Задание 1: выбор проекции. perspective отвечает на вопрос «как видно
    // через объектив», ortho — «как на чертеже». Обеим нужен aspect, иначе
    // изображение растянется по горизонтали.
    const float aspect = float(context.swapchain_extent.width) /
                         float(context.swapchain_extent.height);

    glm::mat4 projection;

    if (state.perspective) {
        projection = glm::perspective(
            glm::radians(state.fovDegrees),
            aspect,
            0.1f,   // near
            100.0f  // far
        );
    } else {
		// Видимая область: от -1 до +1 по вертикали и по aspect по горизонтали.
		// Масштаб в ортографии задаётся растяжением объекта, а не камерой.
		projection = glm::ortho(
			-aspect, aspect,
			-1.0f, 1.0f,
			0.1f,   // near
            100.0f  // far
		);
    }

    glm::mat4 view = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 3.0f),  // глаз
        glm::vec3(0.0f, 0.0f, 0.0f),  // цель
        glm::vec3(0.0f, 1.0f, 0.0f)   // верх
    );
    // В Vulkan ось Y направлена вниз, в GLM — вверх. Отражение возвращает
    // правильную ориентацию; попутно меняет winding, поэтому frontFace
    // в пайплайне выставлен на CLOCKWISE с расчётом на это отражение.
    view[1][1] *= -1.0f;

    // Задание 2: трансформации из интерфейса.
    //
    // Порядок умножения: GLM хранит матрицы column-major, и M * v применяет
    // сначала M, потом v. Поэтому цепочка идёт от последнего преобразования
    // к первому — scale, затем rotate, затем translate. Перестановка
    // translate и scale применит масштабирование к координате сдвига.
    glm::mat4 model = glm::mat4(1.0f);

    model = glm::scale(model, state.scale);
    model = glm::rotate(model, glm::radians(state.rotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(state.rotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, glm::radians(state.rotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));
    model = glm::translate(model, state.position);

    glm::mat4 transform = projection * view * model;

    // Цвет передаётся на каждую грань отдельно, а вершины у соседних граней
    // общие — одним вызовом vkCmdDrawIndexed обойтись нельзя.
    constexpr uint32_t indicesPerTriangle = 3;

    for (uint32_t i = 0; i < 6; ++i) {
        const uint32_t offset = 3 * i;

        const Transform pcData{
            .modelViewProjection = transform,
            .color = faceColors[i],
        };

        vkCmdPushConstants(fd.command_buffer, pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(Transform), &pcData);

        vkCmdDrawIndexed(fd.command_buffer, indicesPerTriangle, 1, offset, 0, 0);
    }

    vkCmdEndRenderPass(fd.command_buffer);
    vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application