#pragma once

struct GLFWwindow;

namespace vt::ui {

// Thin wrapper over the ImGui rendering backend:
//   Windows:      Direct3D 11 (much lighter driver footprint than OpenGL on
//                 most Windows GPUs, which matters for the memory budget)
//   macOS/Linux:  OpenGL 3
class Renderer {
 public:
  // Call before glfwCreateWindow to set the right context hints.
  static void setWindowHints();

  bool init(GLFWwindow* window);
  void newFrame();
  void render(float r, float g, float b);
  void shutdown();

 private:
  GLFWwindow* window_ = nullptr;
};

}  // namespace vt::ui
