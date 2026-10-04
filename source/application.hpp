#pragma once

#include "graphics_internal.hpp"

struct GLFWwindow;

namespace application {

bool initialize();
void shutdown();

void update(double time);
void render(const graphics::internal::FrameData& fd);

void attachWindow(GLFWwindow* window);
void onMouseButton(int button, int action);
void onCursorPos(double xpos, double ypos);

} // namespace application