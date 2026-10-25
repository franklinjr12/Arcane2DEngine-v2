// ImGui context lifecycle must work with no GL context and no window; the
// editor will build on this, but Foundation A only needs compile+link+smoke.
#include <imgui.h>

#include "test_assert.hpp"

int main() {
  IMGUI_CHECKVERSION();
  ImGuiContext* context = ImGui::CreateContext();
  ARCANE_ASSERT_TRUE(context != nullptr);
  ARCANE_ASSERT_TRUE(ImGui::GetCurrentContext() == context);

  ImGuiIO& io = ImGui::GetIO();
  io.DisplaySize = ImVec2(640.0f, 480.0f);

  ImGui::DestroyContext(context);
  ARCANE_ASSERT_TRUE(ImGui::GetCurrentContext() == nullptr);
  return 0;
}
