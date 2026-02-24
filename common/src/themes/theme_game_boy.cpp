#include "nativecore/themes.h"
#include <imgui.h>

namespace nativecore {
namespace themes {

static inline ImVec4 C(float r, float g, float b, float a = 1.0f) {
  return ImVec4(r, g, b, a);
}

void apply_game_boy() {
  ImGuiStyle &style = ImGui::GetStyle();
  ImVec4 *colors = style.Colors;

  // Palette
  const ImVec4 shell_bg = C(0.847f, 0.851f, 0.839f); // #D8D9D6 – main bg
  // const ImVec4 shell_mid = C(0.710f, 0.718f, 0.706f);    // #B5B7B4 – panel
  // bg
  const ImVec4 shell_dark = C(0.541f, 0.549f, 0.537f);   // #8A8C89 – borders
  const ImVec4 shell_deeper = C(0.380f, 0.388f, 0.376f); // #616260 – separators

  // Screen colors
  // const ImVec4 screen_darkest =
  //     C(0.059f, 0.176f, 0.063f); // #0F2D10 – deepest shadow
  // const ImVec4 screen_dark = C(0.141f, 0.318f, 0.137f);   // #245122 – dark
  // tone
  const ImVec4 screen_mid = C(0.278f, 0.494f, 0.251f);    // #477E40 – mid tone
  const ImVec4 screen_light = C(0.569f, 0.718f, 0.424f);  // #91B76C – highlight
  const ImVec4 screen_bright = C(0.690f, 0.816f, 0.533f); // #B0D088 – brightest

  // Button accent
  const ImVec4 btn_base = C(0.549f, 0.082f, 0.298f);   // #8C154C
  const ImVec4 btn_hover = C(0.671f, 0.153f, 0.388f);  // #AB2763
  const ImVec4 btn_active = C(0.420f, 0.047f, 0.220f); // #6B0C38

  const ImVec4 text_primary = C(0.102f, 0.110f, 0.102f);   // #1A1C1A
  const ImVec4 text_secondary = C(0.318f, 0.333f, 0.318f); // #515451

  // Window / Panels
  colors[ImGuiCol_WindowBg] = shell_bg;
  colors[ImGuiCol_PopupBg] = C(0.800f, 0.808f, 0.796f, 0.97f);

  // Borders
  colors[ImGuiCol_Border] = shell_dark;
  colors[ImGuiCol_BorderShadow] = C(0.0f, 0.0f, 0.0f, 0.10f);

  // Text
  colors[ImGuiCol_Text] = text_primary;
  colors[ImGuiCol_TextDisabled] = text_secondary;

  // Frames
  colors[ImGuiCol_FrameBg] = C(0.188f, 0.349f, 0.184f, 0.80f);
  colors[ImGuiCol_FrameBgHovered] = C(0.278f, 0.459f, 0.251f, 0.90f);
  colors[ImGuiCol_FrameBgActive] = C(0.141f, 0.278f, 0.137f, 1.00f);

  // Check Mark & Slider
  colors[ImGuiCol_CheckMark] = screen_bright;
  colors[ImGuiCol_SliderGrab] = screen_light;
  colors[ImGuiCol_SliderGrabActive] = screen_bright;

  // Buttons
  colors[ImGuiCol_Button] = btn_base;
  colors[ImGuiCol_ButtonHovered] = btn_hover;
  colors[ImGuiCol_ButtonActive] = btn_active;

  // Headers
  colors[ImGuiCol_Header] = C(btn_base.x, btn_base.y, btn_base.z, 0.55f);
  colors[ImGuiCol_HeaderHovered] =
      C(btn_hover.x, btn_hover.y, btn_hover.z, 0.80f);
  colors[ImGuiCol_HeaderActive] = btn_active;

  // Separators
  colors[ImGuiCol_Separator] = shell_deeper;
  colors[ImGuiCol_SeparatorHovered] = screen_mid;
  colors[ImGuiCol_SeparatorActive] = screen_light;

  // Tabs
  colors[ImGuiCol_Tab] = C(0.600f, 0.608f, 0.596f, 1.00f);
  colors[ImGuiCol_TabHovered] = btn_hover;
  colors[ImGuiCol_TabActive] = btn_base;
  colors[ImGuiCol_TabUnfocused] = C(0.710f, 0.718f, 0.706f, 0.90f);
  colors[ImGuiCol_TabUnfocusedActive] =
      C(shell_dark.x, shell_dark.y, shell_dark.z, 1.00f);

  // Plots
  colors[ImGuiCol_PlotLines] = screen_light;
  colors[ImGuiCol_PlotLinesHovered] = screen_bright;
  colors[ImGuiCol_PlotHistogram] = screen_mid;
  colors[ImGuiCol_PlotHistogramHovered] = screen_light;

  // Misc
  colors[ImGuiCol_TextSelectedBg] =
      C(screen_mid.x, screen_mid.y, screen_mid.z, 0.45f);
  colors[ImGuiCol_DragDropTarget] = screen_bright;
  colors[ImGuiCol_NavHighlight] = screen_light;
  colors[ImGuiCol_NavWindowingHighlight] =
      C(screen_bright.x, screen_bright.y, screen_bright.z, 0.80f);
  colors[ImGuiCol_NavWindowingDimBg] = C(0.0f, 0.0f, 0.0f, 0.40f);
  colors[ImGuiCol_ModalWindowDimBg] = C(0.0f, 0.0f, 0.0f, 0.35f);

  // table header
  colors[ImGuiCol_TableHeaderBg] = C(0.600f, 0.608f, 0.596f, 1.00f);

  style.ChildRounding = 4.0f;
  style.FrameRounding = 4.0f;
  style.PopupRounding = 4.0f;
  style.ScrollbarRounding = 6.0f;
  style.GrabRounding = 4.0f;
  style.TabRounding = 4.0f;

  style.ItemSpacing = ImVec2(8.0f, 6.0f);
  style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
  style.IndentSpacing = 20.0f;
  style.GrabMinSize = 20.0f;

  style.FrameBorderSize = 1.0f;
  style.PopupBorderSize = 1.0f;
}

} // namespace themes
} // namespace nativecore
