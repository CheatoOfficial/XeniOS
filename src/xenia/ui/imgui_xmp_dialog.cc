/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/imgui_xmp_dialog.h"

#include <cfloat>

#include "third_party/imgui/imgui.h"
#include "xenia/app/emulator_window.h"
#include "xenia/apu/audio_media_player.h"
#include "xenia/emulator.h"
#include "xenia/kernel/xam/apps/xmp_app.h"

namespace xe {
namespace ui {

ImGuiXmpDialog::ImGuiXmpDialog(ImGuiDrawer* drawer,
                               app::EmulatorWindow* emulator_window,
                               hid::InputSystem* input_system)
    : ImGuiGamepadDialog(drawer, input_system),
      emulator_window_(emulator_window) {
  // Initialize volume from current audio player state
  auto emulator = emulator_window_->emulator();
  if (emulator && emulator->audio_media_player()) {
    volume_percent_ = static_cast<int>(
        emulator->audio_media_player()->GetVolume()->load() * 100.0f);
  }
}

void ImGuiXmpDialog::OnClose() {
  if (on_close_callback_) {
    on_close_callback_();
  }
}

void ImGuiXmpDialog::OnDraw(ImGuiIO& io) {
  // Style - white background, black text, Xbox green accents
  const ImVec4 xbox_green(0.063f, 0.486f, 0.063f, 1.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_TitleBg, xbox_green);
  ImGui::PushStyleColor(ImGuiCol_TitleBgActive, xbox_green);
  ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                        ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_SliderGrab, xbox_green);
  ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,
                        ImVec4(0.1f, 0.6f, 0.1f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

  // Center on screen
  ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

  // Ensure window has reasonable minimum width
  float title_width = ImGui::CalcTextSize("XMP Audio Player").x;
  float min_width = (title_width + ImGui::GetStyle().WindowPadding.x * 2 +
                     ImGui::GetFrameHeight()) *
                    2.0f;
  ImGui::SetNextWindowSizeConstraints(ImVec2(min_width, 0),
                                      ImVec2(FLT_MAX, FLT_MAX));

  bool is_open = true;
  if (ImGui::Begin("XMP Audio Player", &is_open,
                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoCollapse |
                       ImGuiWindowFlags_NoScrollbar)) {
    // Handle keyboard escape or gamepad B/Back
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ShouldCloseFromGamepad()) {
      Close();
    }

    auto emulator = emulator_window_->emulator();
    auto audio_player = emulator ? emulator->audio_media_player() : nullptr;

    if (audio_player) {
      bool is_playing = audio_player->IsPlaying();
      bool is_paused = audio_player->IsPaused();

      // Playback controls - centered Play and Pause buttons
      float button_width = ImGui::GetFontSize() * 5.0f;
      float spacing = ImGui::GetStyle().ItemSpacing.x;
      float total_width = button_width * 2 + spacing;
      float start_x = (ImGui::GetWindowWidth() - total_width) * 0.5f;

      ImGui::SetCursorPosX(start_x);

      // Play button - disabled when playing
      if (is_playing) {
        ImGui::BeginDisabled();
      }
      if (ImGui::Button("Play", ImVec2(button_width, 0))) {
        audio_player->Continue();
      }
      if (is_playing) {
        ImGui::EndDisabled();
      }

      ImGui::SameLine();

      // Pause button - disabled when not playing
      if (!is_playing) {
        ImGui::BeginDisabled();
      }
      if (ImGui::Button("Pause", ImVec2(button_width, 0))) {
        audio_player->Pause();
      }
      if (!is_playing) {
        ImGui::EndDisabled();
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Volume control
      ImGui::Text("Volume");
      ImGui::SetNextItemWidth(-1);  // Use full available width
      if (ImGui::SliderInt("##volume", &volume_percent_, 0, 100, "%d%%",
                           ImGuiSliderFlags_None)) {
        audio_player->SetVolume(volume_percent_ / 100.0f);
      }
      if (!ImGui::IsItemActive()) {
        volume_percent_ =
            static_cast<int>(audio_player->GetVolume()->load() * 100.0f);
      }

    } else {
      ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f),
                         "No audio player available");
    }

    ImGui::End();
  }

  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor(14);

  if (!is_open) {
    Close();
  }
}

}  // namespace ui
}  // namespace xe
