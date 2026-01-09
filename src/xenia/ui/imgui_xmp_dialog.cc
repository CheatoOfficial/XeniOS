/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/imgui_xmp_dialog.h"

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
  PollGamepad();

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
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 16));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);

  // Center on screen
  ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Always);

  bool is_open = true;
  if (ImGui::Begin("XMP Audio Player", &is_open,
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoCollapse)) {
    // Handle keyboard
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      Close();
    }

    auto emulator = emulator_window_->emulator();
    auto audio_player = emulator ? emulator->audio_media_player() : nullptr;

    if (audio_player) {
      using xmp_state = kernel::xam::apps::XmpApp::State;

      // Status display
      ImGui::PushStyleColor(ImGuiCol_Text, xbox_green);
      ImGui::Text("Status");
      ImGui::PopStyleColor();

      ImGui::Indent(10);
      ImGui::Text("Audio player status:");
      ImGui::SameLine();

      switch (audio_player->GetState()) {
        case xmp_state::kIdle:
          ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Idle");
          break;
        case xmp_state::kPaused:
          ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Paused");
          break;
        case xmp_state::kPlaying:
          ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Playing");
          break;
        default:
          ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Unknown");
          break;
      }
      ImGui::Unindent(10);

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Playback controls
      ImGui::PushStyleColor(ImGuiCol_Text, xbox_green);
      ImGui::Text("Playback");
      ImGui::PopStyleColor();

      ImGui::Indent(10);
      if (audio_player->IsPlaying()) {
        if (ImGui::Button("Pause", ImVec2(100, 30))) {
          audio_player->Pause();
        }
      } else if (audio_player->IsPaused()) {
        if (ImGui::Button("Resume", ImVec2(100, 30))) {
          audio_player->Continue();
        }
      } else {
        ImGui::BeginDisabled();
        ImGui::Button("Resume", ImVec2(100, 30));
        ImGui::EndDisabled();
      }
      ImGui::Unindent(10);

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Volume control
      ImGui::PushStyleColor(ImGuiCol_Text, xbox_green);
      ImGui::Text("Volume");
      ImGui::PopStyleColor();

      ImGui::Indent(10);
      ImGui::SetNextItemWidth(250);
      if (ImGui::SliderInt("##volume", &volume_percent_, 0, 100, "%d%%",
                           ImGuiSliderFlags_None)) {
        // Convert 0-100 back to 0.0-1.0 for the audio player
        audio_player->SetVolume(volume_percent_ / 100.0f);
      }
      // Only sync from player when slider is not being interacted with
      if (!ImGui::IsItemActive()) {
        volume_percent_ =
            static_cast<int>(audio_player->GetVolume()->load() * 100.0f);
      }
      ImGui::Unindent(10);

    } else {
      ImGui::TextColored(ImVec4(0.8f, 0.4f, 0.4f, 1.0f),
                         "No audio player available");
    }

    ImGui::End();
  }

  ImGui::PopStyleVar(3);
  ImGui::PopStyleColor(14);

  if (!is_open) {
    Close();
  }
}

}  // namespace ui
}  // namespace xe
