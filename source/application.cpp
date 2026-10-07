#include "application.hpp"
#include "vulkan/vulkan_core.h"

#include <imgui.h>
#include <algorithm>
#include <fstream>
#include <vector>
#include <iostream>
#include <cstring>
#include <numbers>

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

// Раскладка вершинного буфера; должна совпадать с attributeDescriptions
// и входами вершинного шейдера.
struct Vertex {
	float position[3];
	float color[3];
};

// Процедурный цвет вершины из её локальной позиции: по горизонтали —
// градиент от красного к синему, по вертикали — чем выше, тем светлее.
glm::vec3 proceduralVertexColor(const glm::vec3& localPosition) {
	const float height = localPosition.y + 0.5f;  // 0..1 внутри фигуры

	return {
		1.0f - localPosition.x,
		0.5f + 0.5f * height,
		localPosition.x + 0.5f
	};
}

// Позиции вершин пирамиды в локальной системе координат.
constexpr glm::vec3 vertexPositions[5] = {
	{ 0.5f, -0.5f,  0.5f},   // 0
	{ 0.5f, -0.5f, -0.5f},   // 1
	{-0.5f, -0.5f, -0.5f},   // 2
	{-0.5f, -0.5f,  0.5f},   // 3
	{ 0.0f,  0.5f,  0.0f},   // 4 — вершина
};

// Цвет каждой вершины считается процедурно из её локальной позиции.
const std::vector<Vertex> vertices = [] {
	std::vector<Vertex> result;
	result.reserve(std::size(vertexPositions));

	for (const glm::vec3& position : vertexPositions) {
		const glm::vec3 color = proceduralVertexColor(position);

		result.push_back(Vertex{
			{position.x, position.y, position.z},
			{color.r, color.g, color.b}
		});
	}

	return result;
}();

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
	{
		.location = 1,
		.binding = 0,
		.format = VK_FORMAT_R32G32B32_SFLOAT,
		.offset = offsetof(Vertex, color)
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

// Данные, передаваемые в шейдеры через push constants каждый кадр.
struct Transform {
	glm::mat4 modelViewProjection;
	glm::vec3 color;
};
static_assert(sizeof(Transform) == 76, "раскладка push constants разошлась с шейдером");

// Начальные значения трансформаций.
constexpr glm::vec3 initialPosition{0.0f, 0.0f, 0.0f};
constexpr glm::vec3 initialRotationDegrees{0.0f, 0.0f, 0.0f};
constexpr glm::vec3 initialScale{1.0f, 1.0f, 1.0f};

constexpr float pi = static_cast<float>(std::numbers::pi);

// Геометрия спирали и границы ползунков. Высота задана константой: ползунок
// радиуса меняет только круг по XZ, а кадр считается по максимумам, поэтому
// камера не «прыгает» при изменении ползунков.
constexpr float spiralHalfHeight = 5.0f;      // полуразмах спирали по Y
constexpr float fovDegrees = 45.0f;           // угол обзора по вертикали
constexpr float maxOrbitRadius = 5.0f;        // верхняя граница ползунка радиуса
constexpr float minSpiralTurns = 0.5f;        // нижняя граница ползунка витков
constexpr float cameraObjectAllowance = 0.8f; // запас кадра на габарит пирамиды
constexpr float framePadding = 1.05f;         // доля кадра, занимаемая сценой

struct SceneState {
	// Проекция
	bool perspective = true;

	// Трансформации.
	glm::vec3 position = initialPosition;
	glm::vec3 rotationDegrees = initialRotationDegrees;  // доводка поверх автоориентации
	glm::vec3 scale = initialScale;

	// Траектория.
	bool animationPaused = false;
	float animationTime = 0.0f;
	float animationSpeed = 1.0f;
	float orbitRadius = 1.5f;
	float spiralTurns = 2.0f;

	// Цвет-множитель, умножается на процедурный цвет вершины
	// во фрагментном шейдере. Белый оставляет исходные цвета.
	glm::vec3 tint = glm::vec3(1.0f);
};

SceneState state;

// Точка траектории для прогресса p в диапазоне [0, 1].
glm::vec3 trajectoryPoint(float progress) {
	const float angle = progress * 2.0f * pi * state.spiralTurns;

	return {
		std::cos(angle) * state.orbitRadius,
		std::sin(progress * pi) * spiralHalfHeight,
		std::sin(angle) * state.orbitRadius,
	};
}

// Касательная к траектории — производная точки по прогрессу, без нормировки.
glm::vec3 trajectoryTangent(float progress) {
	const float angle = progress * 2.0f * pi * state.spiralTurns;
	const float angularSpeed = 2.0f * pi * state.spiralTurns;

	return {
		-std::sin(angle) * state.orbitRadius * angularSpeed,
		std::cos(progress * pi) * pi * spiralHalfHeight,
		std::cos(angle) * state.orbitRadius * angularSpeed,
	};
}

// За цикл пирамида обходит окружность радиуса orbitRadius ровно spiralTurns
// раз, поэтому длина пути по XZ постоянна и равна 2*pi*r*turns. Делим
// скорость из ползунка на эту длину — и физическая скорость перестаёт
// зависеть от радиуса и числа витков.
float cycleDuration() {
	const float pathLength = 2.0f * pi * state.orbitRadius * state.spiralTurns;
	const float speed = std::max(state.animationSpeed, 0.01f);

	return std::max(pathLength / speed, 0.05f);
}

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

// Запоминаем окно и подписываемся на события мыши для вращения камеры.
void attachWindow(GLFWwindow* const glfwWindow) {
	window = glfwWindow;

	glfwSetMouseButtonCallback(window, [](GLFWwindow*, int button, int action, int) {
		application::onMouseButton(button, action);
	});

	glfwSetCursorPosCallback(window, [](GLFWwindow*, double xpos, double ypos) {
		application::onCursorPos(xpos, ypos);
	});
}

// Вращение по зажатой левой кнопке мыши.
void onMouseButton(int button, int action) {
	if (button != GLFW_MOUSE_BUTTON_LEFT) {
		return;
	}

	mouseButtonHeld = (action == GLFW_PRESS);

	if (mouseButtonHeld && window != nullptr) {
		glfwGetCursorPos(window, &lastMouseX, &lastMouseY);
	}
}

// Перетаскивание курсора превращается в поворот фигуры. Пока курсор над
// интерфейсом ImGui, вращение не выполняется, но позиция запоминается,
// чтобы при возврате на сцену не было скачка.
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

// Чтение бинарного файла целиком (для скомпилированных шейдеров).
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
		.vertexAttributeDescriptionCount = 2,
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

// Создание буфера, доступного из CPU: память выделяется VMA, данные
// копируются в отображённую память и отображение снимается.
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

// Истинно, пока пользователь тянет ползунок таймлайна: на время перетаскивания
// автопрокрутка ставится на паузу, чтобы ползунок не «убегал» из-под курсора.
static bool timelineDragging = false;

// Панель управления ImGui
static void drawControlPanel() {
	ImGui::Begin("Controls");

	if (ImGui::Checkbox("Перспективная проекция", &state.perspective)) {
	}

	ImGui::Separator();

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

	ImGui::Separator();
	ImGui::TextUnformatted("Траектория");
	if (ImGui::Button(state.animationPaused ? "Продолжить" : "Пауза")) {
		state.animationPaused = !state.animationPaused;
	}
	ImGui::SameLine();
	if (ImGui::Button("Сброс анимации")) {
		state.animationTime = 0.0f;
		state.animationPaused = false;
	}
	// Шкала таймлайна равна длительности цикла и перестраивается вместе с ней.
	ImGui::SliderFloat("Таймлайн", &state.animationTime, 0.0f, cycleDuration(), "%.2f s");
	if (ImGui::IsItemActive()) {
		state.animationPaused = true;
		timelineDragging = true;
	} else if (timelineDragging) {
		timelineDragging = false;
	}
	ImGui::SliderFloat("Скорость", &state.animationSpeed, 0.1f, 5.0f, "%.2fx");
	ImGui::SliderFloat("Радиус спирали", &state.orbitRadius, 0.2f, maxOrbitRadius, "%.2f");
	ImGui::SliderFloat("Плотность витков", &state.spiralTurns, minSpiralTurns, 6.0f, "%.1f");

	ImGui::Separator();
	ImGui::TextUnformatted("Цвет");
	// Множитель на процедурные цвета вершин, а не самостоятельный цвет.
	ImGui::ColorEdit3("Оттенок", &state.tint.x, ImGuiColorEditFlags_NoInputs);
	ImGui::SameLine();
	if (ImGui::Button("Сбросить")) {
		state.tint = glm::vec3(1.0f);
	}

	ImGui::End();
}

static double previousUpdateTime = 0.0;
static bool hasPreviousUpdateTime = false;

void update(double time) {
	if (!hasPreviousUpdateTime) {
		hasPreviousUpdateTime = true;
		previousUpdateTime = time;
	}

	const double deltaTime = time - previousUpdateTime;
	previousUpdateTime = time;

	// Время идёт равномерно, а пройденное расстояние = время * скорость.
	// Поэтому скорость в единицах в секунду задаётся исключительно ползунком.
	if (!state.animationPaused) {
		state.animationTime += static_cast<float>(deltaTime);
	}

	// После смены скорости или параметров траектории длительность цикла
	// меняется, и время может выйти за её новые границы.
	const float duration = cycleDuration();
	state.animationTime -= std::floor(state.animationTime / duration) * duration;
	state.animationTime = std::clamp(state.animationTime, 0.0f, duration);

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

	// Габарит спирали: полуразмах по Y и радиус по XZ, плюс запас на пирамиду.
	// Высота по Y постоянна, ширина берётся по максимуму ползунка радиуса,
	// поэтому кадр не зависит от текущего значения ползунка.
	const float frameHalfHeight = spiralHalfHeight + cameraObjectAllowance;
	const float frameHalfWidth = maxOrbitRadius + cameraObjectAllowance;

	// Для точки (x, y, z) при камере на расстоянии d по оси Z условие попадания
	// в перспективный кадр: |y| <= tan(halfFov) * (d - z). Худший случай по
	// вертикали и горизонтали даёт две нижние границы для d, берём большую.
	const float fovRadians = glm::radians(fovDegrees);
	const float tanVertical = std::tan(fovRadians * 0.5f);
	const float tanHorizontal = tanVertical * aspect;

	const float distanceForVertical =
		maxOrbitRadius + frameHalfHeight / tanVertical;
	const float distanceForHorizontal =
		maxOrbitRadius + frameHalfWidth / tanHorizontal;

	const float distance =
		framePadding * std::max(distanceForVertical, distanceForHorizontal);
	const float farPlane = distance + frameHalfHeight + 1.0f;

	glm::mat4 projection;

	if (state.perspective) {
		projection = glm::perspective(
			fovRadians,
			aspect,
			0.1f,      // near
			farPlane   // far
		);
	} else {
		// В ортографии размер кадра не зависит от расстояния, поэтому
		// расширяем саму видимую область до габарита анимации.
		projection = glm::ortho(
			-frameHalfWidth, frameHalfWidth,
			-frameHalfHeight, frameHalfHeight,
			0.1f,      // near
			farPlane   // far
		);
	}

	glm::mat4 view = glm::lookAt(
		glm::vec3(0.0f, 0.0f, distance),  // глаз
		glm::vec3(0.0f, 0.0f, 0.0f),      // цель
		glm::vec3(0.0f, 1.0f, 0.0f)       // верх
	);
	// В Vulkan ось Y направлена вниз, в GLM — вверх. Отражение возвращает
	// правильную ориентацию; попутно меняет winding, поэтому frontFace
	// в пайплайне выставлен на CLOCKWISE с расчётом на это отражение.
	view[1][1] *= -1.0f;

	// Параметр траектории идёт от времени через длительность цикла, а она уже
	// учитывает скорость из ползунка. Радиус и число витков меняют форму пути
	// и длительность цикла, но не скорость движения.
	const float helixProgress = state.animationTime / cycleDuration();

	const glm::vec3 orbitOffset = trajectoryPoint(helixProgress);

	const glm::vec3 objectPosition = state.position + orbitOffset;

	// Касательная — направление движения: вершина пирамиды смотрит вдоль неё.
	const glm::vec3 tangent = glm::normalize(trajectoryTangent(helixProgress));

	// Горизонтальное направление от пирамиды к оси спирали. Ось вертикальна
	// и проходит через базовую позицию, поэтому достаточно обнулить Y.
	glm::vec3 inward = glm::vec3(
		state.position.x - objectPosition.x,
		0.0f,
		state.position.z - objectPosition.z
	);

	if (glm::dot(inward, inward) < 1e-8f) {
		inward = glm::vec3(0.0f, 0.0f, -1.0f);
	} else {
		inward = glm::normalize(inward);
	}

	// Касательная всегда перпендикулярна радиальному направлению: её
	// горизонтальная составляющая направлена по движению, а вертикальная не
	// даёт вклада в скалярное произведение с горизонтальным вектором.
	const glm::vec3 side = glm::normalize(glm::cross(tangent, inward));

	// Столбцы матрицы — образы локальных осей. Локальная +Y (вершина) идёт
	// вдоль движения, а локальная диагональ (X+Z)/√2 — проекция бокового
	// ребра — смотрит на ось, поэтому к оси обращено ребро, а не грань.
	// Конструкция даёт определитель +1: матрица остаётся поворотом и не
	// переворачивает отсечение граней.
	constexpr float invSqrt2 = 0.70710678118654752f;

	const glm::mat4 orientation{
		glm::vec4((inward + side) * invSqrt2, 0.0f),  // образ локальной X
		glm::vec4(tangent, 0.0f),                     // образ локальной Y
		glm::vec4((inward - side) * invSqrt2, 0.0f),  // образ локальной Z
		glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
	};

	glm::mat4 local = glm::mat4(1.0f);

	local = glm::scale(local, state.scale);
	local = glm::rotate(local, glm::radians(state.rotationDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f));
	local = glm::rotate(local, glm::radians(state.rotationDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
	local = glm::rotate(local, glm::radians(state.rotationDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f));

	// Позиция применяется в мировом пространстве последней, иначе повороты
	// и масштаб сдвинут центр орбиты за собой.
	const glm::mat4 model =
		glm::translate(glm::mat4(1.0f), objectPosition) * orientation * local;

	const glm::mat4 transform = projection * view * model;

	const Transform pcData{
		.modelViewProjection = transform,
		.color = state.tint,
	};

	vkCmdPushConstants(fd.command_buffer, pipelineLayout,
	                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
	                   0, sizeof(Transform), &pcData);

	vkCmdDrawIndexed(fd.command_buffer,
	                 static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

	vkCmdEndRenderPass(fd.command_buffer);
	vkEndCommandBuffer(fd.command_buffer);
}

} // namespace application