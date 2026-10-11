#include "ui/Renderer.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>

#include "core/Log.h"

#if defined(_WIN32)

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <d3d11.h>
#include <imgui_impl_dx11.h>

namespace vt::ui {

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_rtv = nullptr;
int g_width = 0, g_height = 0;

void createRenderTarget() {
  ID3D11Texture2D* backBuffer = nullptr;
  if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
    g_device->CreateRenderTargetView(backBuffer, nullptr, &g_rtv);
    backBuffer->Release();
  }
}

void releaseRenderTarget() {
  if (g_rtv) {
    g_rtv->Release();
    g_rtv = nullptr;
  }
}

bool createDevice(HWND hwnd, DXGI_SWAP_EFFECT effect, D3D_DRIVER_TYPE driver) {
  DXGI_SWAP_CHAIN_DESC sd{};
  sd.BufferCount = 2;
  sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.OutputWindow = hwnd;
  sd.SampleDesc.Count = 1;
  sd.Windowed = TRUE;
  sd.SwapEffect = effect;
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
  D3D_FEATURE_LEVEL got;
  HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd,
                                             &g_swapChain, &g_device, &got, &g_context);
  return SUCCEEDED(hr);
}

}  // namespace

void Renderer::setWindowHints() { glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); }

bool Renderer::init(GLFWwindow* window) {
  window_ = window;
  HWND hwnd = glfwGetWin32Window(window);
  // Prefer the modern flip model; fall back for older systems and to WARP
  // (software) if there's no usable GPU.
  if (!createDevice(hwnd, DXGI_SWAP_EFFECT_FLIP_DISCARD, D3D_DRIVER_TYPE_HARDWARE) &&
      !createDevice(hwnd, DXGI_SWAP_EFFECT_DISCARD, D3D_DRIVER_TYPE_HARDWARE) &&
      !createDevice(hwnd, DXGI_SWAP_EFFECT_DISCARD, D3D_DRIVER_TYPE_WARP)) {
    log::error("Direct3D 11 initialization failed");
    return false;
  }
  createRenderTarget();
  glfwGetFramebufferSize(window, &g_width, &g_height);
  ImGui_ImplGlfw_InitForOther(window, true);
  ImGui_ImplDX11_Init(g_device, g_context);
  return true;
}

void Renderer::newFrame() {
  ImGui_ImplDX11_NewFrame();
  ImGui_ImplGlfw_NewFrame();
}

void Renderer::render(float r, float g, float b) {
  int w, h;
  glfwGetFramebufferSize(window_, &w, &h);
  if (w <= 0 || h <= 0) return;
  if (w != g_width || h != g_height) {
    releaseRenderTarget();
    g_swapChain->ResizeBuffers(0, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_UNKNOWN, 0);
    createRenderTarget();
    g_width = w;
    g_height = h;
  }
  const float clear[4] = {r, g, b, 1.0f};
  g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
  g_context->ClearRenderTargetView(g_rtv, clear);
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
  g_swapChain->Present(1, 0);
}

void Renderer::shutdown() {
  ImGui_ImplDX11_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  releaseRenderTarget();
  if (g_swapChain) g_swapChain->Release();
  if (g_context) g_context->Release();
  if (g_device) g_device->Release();
  g_swapChain = nullptr;
  g_context = nullptr;
  g_device = nullptr;
}

}  // namespace vt::ui

#else  // OpenGL 3

#include <imgui_impl_opengl3.h>
#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

namespace vt::ui {

void Renderer::setWindowHints() {
#if defined(__APPLE__)
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif
}

bool Renderer::init(GLFWwindow* window) {
  window_ = window;
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  ImGui_ImplGlfw_InitForOpenGL(window, true);
#if defined(__APPLE__)
  return ImGui_ImplOpenGL3_Init("#version 150");
#else
  return ImGui_ImplOpenGL3_Init("#version 130");
#endif
}

void Renderer::newFrame() {
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
}

void Renderer::render(float r, float g, float b) {
  int w, h;
  glfwGetFramebufferSize(window_, &w, &h);
  glViewport(0, 0, w, h);
  glClearColor(r, g, b, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  glfwSwapBuffers(window_);
}

void Renderer::shutdown() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
}

}  // namespace vt::ui

#endif
