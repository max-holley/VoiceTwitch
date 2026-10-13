#include "ui/AppIcon.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifdef _WIN32
#include <windows.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#elif !defined(__APPLE__)
#include "AppIconPixels.h"
#endif

namespace vt::ui {

void setAppIcon(GLFWwindow* window) {
#ifdef _WIN32
  // The same embedded ICO supplies Explorer, shortcuts, Alt-Tab and taskbar.
  // Shared resource handles are owned by Windows and need no DestroyIcon.
  HWND hwnd = glfwGetWin32Window(window);
  HINSTANCE instance = GetModuleHandleW(nullptr);
  auto loadIcon = [&](int width, int height) {
    return reinterpret_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(101),
        IMAGE_ICON, width, height, LR_SHARED));
  };
  if (HICON icon = loadIcon(GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON)))
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
  if (HICON icon = loadIcon(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON)))
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
#elif !defined(__APPLE__)
  // Wayland uses its desktop entry; glfwSetWindowIcon is supported on X11.
  if (glfwGetPlatform() == GLFW_PLATFORM_X11) {
    GLFWimage icon{64, 64, appIconPixels};
    glfwSetWindowIcon(window, 1, &icon);
  }
#else
  // macOS obtains the Dock icon from VoiceTwitch.app/Contents/Resources.
  (void)window;
#endif
}

}  // namespace vt::ui
