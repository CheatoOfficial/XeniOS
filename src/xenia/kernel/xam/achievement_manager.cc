/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/achievement_manager.h"
#include "xenia/base/platform.h"
#include "xenia/emulator.h"
#include "xenia/gpu/graphics_system.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/achievement_backends/gpd_achievement_backend.h"
#include "xenia/kernel/xam/xdbf/gpd_info.h"
#include "xenia/ui/audio_helper.h"
#include "xenia/ui/imgui_guest_notification.h"
#if XE_PLATFORM_IOS
#include "xenia/ui/achievement_notification_payload.h"
#include "xenia/ui/ios/app/windowed_app_context_ios.h"
#endif  // XE_PLATFORM_IOS

DEFINE_bool(show_achievement_notification, true,
            "Show achievement notification on screen.", "UI");

DEFINE_bool(achievement_notification_position_by_game, false,
            "Use game-specified notification position for achievements. "
            "When disabled, achievements always appear at center-bottom.",
            "UI");

DEFINE_string(
    default_achievements_backend, "GPD",
    "Defines which achievements backend should be used as an default. "
    "Possible options: GPD.",
    "Kernel");

DECLARE_int32(user_language);

namespace xe {
namespace kernel {
namespace xam {

AchievementManager::AchievementManager() {
  default_achievements_backend_ = std::make_unique<GpdAchievementBackend>();

  // Add any optional backend here.
};
void AchievementManager::EarnAchievement(const uint32_t user_index,
                                         const uint32_t title_id,
                                         const uint32_t achievement_id) const {
  const auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return;
  }

  EarnAchievement(user->xuid(), title_id, achievement_id);
};

void AchievementManager::EarnAchievement(const uint64_t xuid,
                                         const uint32_t title_id,
                                         const uint32_t achievement_id) const {
  if (!DoesAchievementExist(achievement_id)) {
    XELOGW(
        "{}: Achievement with ID: {} for title: {:08X} doesn't exist in "
        "database!",
        __func__, achievement_id, title_id);
    return;
  }
  // Always send request to unlock in 3PP backends. It's up to them to check if
  // achievement was unlocked
  for (auto& backend : achievement_backends_) {
    backend->EarnAchievement(xuid, title_id, achievement_id);
  }

  if (default_achievements_backend_->IsAchievementUnlocked(xuid, title_id,
                                                           achievement_id)) {
    return;
  }

  default_achievements_backend_->EarnAchievement(xuid, title_id,
                                                 achievement_id);

  if (!cvars::show_achievement_notification) {
    return;
  }

  const auto achievement = default_achievements_backend_->GetAchievementInfo(
      xuid, title_id, achievement_id);

  if (!achievement) {
    // Something went really wrong!
    return;
  }
  ShowAchievementEarnedNotification(xuid, title_id, &achievement.value());
}

void AchievementManager::LoadTitleAchievements(const uint64_t xuid) const {
  default_achievements_backend_->LoadAchievementsData(xuid);
}

const std::optional<Achievement> AchievementManager::GetAchievementInfo(
    const uint64_t xuid, const uint32_t title_id,
    const uint32_t achievement_id) const {
  return default_achievements_backend_->GetAchievementInfo(xuid, title_id,
                                                           achievement_id);
}

const std::vector<Achievement> AchievementManager::GetTitleAchievements(
    const uint64_t xuid, const uint32_t title_id) const {
  return default_achievements_backend_->GetTitleAchievements(xuid, title_id);
}

const std::span<const uint8_t> AchievementManager::GetAchievementIcon(
    const uint64_t xuid, const uint32_t title_id,
    const uint32_t achievement_id) const {
  return default_achievements_backend_->GetAchievementIcon(xuid, title_id,
                                                           achievement_id);
}

const std::optional<TitleAchievementsProfileInfo>
AchievementManager::GetTitleAchievementsInfo(const uint64_t xuid,
                                             const uint32_t title_id) const {
  TitleAchievementsProfileInfo info = {};

  const auto achievements = GetTitleAchievements(xuid, title_id);

  if (achievements.empty()) {
    return std::nullopt;
  }

  info.achievements_count = static_cast<uint32_t>(achievements.size());

  for (const auto& entry : achievements) {
    if (!entry.IsUnlocked()) {
      continue;
    }

    info.unlocked_achievements_count++;
    info.gamerscore += entry.gamerscore;
  }

  return info;
}

bool AchievementManager::DoesAchievementExist(
    const uint32_t achievement_id) const {
  return kernel_state()->xam_state()->spa_info()->GetAchievement(
      achievement_id);
}

void AchievementManager::ShowAchievementEarnedNotification(
    uint64_t xuid, uint32_t title_id, const Achievement* achievement) const {
  const std::string title = "Achievement unlocked";
  const std::string description =
      fmt::format("{}G - {}", achievement->gamerscore,
                  xe::to_utf8(achievement->achievement_name));

  const Emulator* emulator = kernel_state()->emulator();
  ui::Window* display_window = emulator ? emulator->display_window() : nullptr;
  if (!display_window) {
    XELOGI("Achievement notification skipped: no display window available");
    return;
  }
  ui::WindowedAppContext* app_context = &display_window->app_context();
  ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

  // Use game-specified position if enabled, otherwise default to center-bottom
  const uint8_t position = cvars::achievement_notification_position_by_game
                               ? kernel_state()->notification_position_
                               : 2;

#if XE_PLATFORM_IOS
  static constexpr size_t kMaxIOSAchievementIconBytes = 512 * 1024;
  ui::AchievementNotificationPayload payload;
  payload.user_index = 0;
  payload.position_id = position;
  payload.xuid = xuid;
  payload.title_id = title_id;
  payload.achievement_id = achievement->achievement_id;
  payload.gamerscore = achievement->gamerscore;
  payload.title = "Achievement unlocked";
  payload.description = description;
  const auto icon = default_achievements_backend_->GetAchievementIcon(
      xuid, title_id, achievement->achievement_id);
  if (icon.size() <= kMaxIOSAchievementIconBytes) {
    payload.icon_data.assign(icon.begin(), icon.end());
  } else {
    XELOGW("Achievement notification icon skipped: {} bytes exceeds cap",
           icon.size());
  }

  app_context->CallInUIThread([app_context, imgui_drawer, title, description,
                               position, payload = std::move(payload)]() {
#else
  app_context->CallInUIThread([imgui_drawer, title, description, position]() {
#endif  // XE_PLATFORM_IOS
    // Play achievement sound
    ui::AudioHelper::Instance().PlayAchievementSound();

#if XE_PLATFORM_IOS
    auto* ios_context = dynamic_cast<ui::IOSWindowedAppContext*>(app_context);
    if (ios_context && ios_context->PresentAchievementNotification(payload)) {
      return;
    }
#endif  // XE_PLATFORM_IOS

    if (!imgui_drawer) {
      XELOGI("Achievement notification skipped: no ImGui drawer available");
      return;
    }

    // Show notification
    new ui::AchievementNotificationWindow(imgui_drawer, title, description, 0,
                                          position);
  });
}

}  // namespace xam
}  // namespace kernel
}  // namespace xe
