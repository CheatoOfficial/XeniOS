/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_EMULATOR_WINDOW_H_
#define XENIA_APP_EMULATOR_WINDOW_H_

#include <memory>
#include <string>

#include <QPointer>

class QTimer;

#include "xenia/emulator.h"
#include "xenia/gpu/command_processor.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/immediate_drawer.h"
#include "xenia/ui/menu_item.h"
#include "xenia/ui/presenter.h"
#include "xenia/ui/profile_dialogs.h"
#include "xenia/ui/window.h"
#include "xenia/ui/window_listener.h"
#include "xenia/ui/windowed_app_context.h"
#include "xenia/xbox.h"

namespace xe {
namespace app {

struct RecentTitleEntry {
  std::string title_name;
  std::filesystem::path path_to_file;
  std::time_t last_run_time;
};

class EmulatorWindow {
 public:
  using steady_clock = std::chrono::steady_clock;  // stdlib steady clock

  enum : size_t {
    // The UI is on top of the game and is open in special cases, so
    // lowest-priority.
    kZOrderHidInput,
    kZOrderImGui,
    kZOrderProfiler,
    // Emulator window controls are expected to be always accessible by the
    // user, so highest-priority.
    kZOrderEmulatorWindowInput,
  };

  virtual ~EmulatorWindow();

  static std::unique_ptr<EmulatorWindow> Create(
      Emulator* emulator, ui::WindowedAppContext& app_context, uint32_t width,
      uint32_t height, bool is_game_process = false);

  std::unique_ptr<xe::threading::Thread> Gamepad_HotKeys_Listener;

  int32_t selected_title_index = -1;

  static constexpr int64_t diff_in_ms(
      const steady_clock::time_point t1,
      const steady_clock::time_point t2) noexcept {
    using ms = std::chrono::milliseconds;
    return std::chrono::duration_cast<ms>(t1 - t2).count();
  }

  steady_clock::time_point last_mouse_up = steady_clock::now();
  steady_clock::time_point last_mouse_down = steady_clock::now();

  Emulator* emulator() const { return emulator_; }
  ui::WindowedAppContext& app_context() const { return app_context_; }
  ui::Window* window() const { return window_.get(); }
  ui::ImGuiDrawer* imgui_drawer() const { return imgui_drawer_.get(); }
  bool is_game_process() const { return is_game_process_; }

  ui::Presenter* GetGraphicsSystemPresenter() const;
  void SetupGraphicsSystemPresenterPainting();
  void ShutdownGraphicsSystemPresenterPainting();

  void OnEmulatorInitialized();

  void LaunchTitleInNewProcess(const std::filesystem::path& path_to_file,
                               bool for_launch_data = false,
                               uint32_t title_id = 0);
  xe::X_STATUS RunTitle(const std::filesystem::path& path_to_file);
  void UpdateTitle();
  bool HasRunningChildProcess();
  void CheckChildProcessStatus();
  void ScheduleChildProcessCheck();

  // Keyboard forwarding for child processes
  void SendKeyToChild(ui::VirtualKey key, bool ctrl = false, bool alt = false,
                      bool shift = false);
  void SendCommandToChild(const std::string& command);
  void ExecuteOrForward(std::function<void()> local_action, ui::VirtualKey key,
                        bool ctrl = false, bool alt = false,
                        bool shift = false);

  void AddRecentlyLaunchedTitle(std::filesystem::path path_to_file,
                                std::string title_name);

  void SetFullscreen(bool fullscreen);
  void ToggleFullscreen();
  void SetInitializingShaderStorage(bool initializing);

  void TakeScreenshot();
  void ExportScreenshot(const xe::ui::RawImage& image);
  void SaveImage(const std::filesystem::path& path,
                 const xe::ui::RawImage& image);

  void ToggleProfilesConfigDialog();
  void ToggleXMPConfigDialog();
  void ToggleConfigDialog();
  void OpenConfigDialog(const std::string& category = "");
  void ToggleControllerVibration();
  void SetHotkeysState(bool enabled) { disable_hotkeys_ = !enabled; }
  void FileOpen();
  const std::vector<RecentTitleEntry>& GetRecentlyLaunchedTitles() const {
    return recently_launched_titles_;
  }

  // Helper methods for updating cvars from Qt dialogs (public for Qt dialogs)
  void UpdateAntiAliasingCvar(gpu::CommandProcessor::SwapPostEffect effect);
  void UpdateScalingAndSharpeningCvar(
      ui::Presenter::GuestOutputPaintConfig::Effect effect);
  void UpdateFsrSharpnessCvar(float value);
  void UpdateCasSharpnessCvar(float value);
  void UpdateDitherCvar(bool value);

  // Types of button functions for hotkeys.
  enum class ButtonFunctions {
    ToggleFullscreen,
    RunTitle,
    CpuTimeScalarSetHalf,
    CpuTimeScalarSetDouble,
    CpuTimeScalarReset,
    ClearGPUCache,
    ToggleControllerVibration,
    ClearMemoryPageState,
    ReadbackResolve,
    ToggleLogging,
    IncTitleSelect,
    DecTitleSelect,
    Unknown
  };

  class ControllerHotKey {
   public:
    // If true the hotkey can be activated while a title is running, otherwise
    // false.
    bool title_passthru;

    // If true vibrate the controller after activating the hotkey, otherwise
    // false.
    bool rumble;
    std::string pretty;
    ButtonFunctions function;

    ControllerHotKey(ButtonFunctions fn = ButtonFunctions::Unknown,
                     std::string pretty = "", bool rumble = false,
                     bool active = true) {
      function = fn;
      this->pretty = pretty;
      title_passthru = active;
      this->rumble = rumble;
    }
  };

 private:
  class EmulatorWindowListener final : public ui::WindowListener,
                                       public ui::WindowInputListener {
   public:
    explicit EmulatorWindowListener(EmulatorWindow& emulator_window)
        : emulator_window_(emulator_window) {}

    void OnClosing(ui::UIEvent& e) override;
    void OnFileDrop(ui::FileDropEvent& e) override;
    void OnGotFocus(ui::UISetupEvent& e) override;
    void OnResize(ui::UISetupEvent& e) override;

    void OnKeyDown(ui::KeyEvent& e) override;

    void OnMouseDown(ui::MouseEvent& e) override;
    void OnMouseUp(ui::MouseEvent& e) override;
    void OnMouseDoubleClick(ui::MouseEvent& e) override;

   private:
    EmulatorWindow& emulator_window_;
  };

  explicit EmulatorWindow(Emulator* emulator,
                          ui::WindowedAppContext& app_context, uint32_t width,
                          uint32_t height, bool is_game_process = false);

  bool Initialize();

  // For comparisons, use GetSwapPostEffectForCvarValue instead as the default
  // fallback may be used for multiple values.
  static const char* GetCvarValueForSwapPostEffect(
      gpu::CommandProcessor::SwapPostEffect effect);
  static gpu::CommandProcessor::SwapPostEffect GetSwapPostEffectForCvarValue(
      const std::string& cvar_value);
  // For comparisons, use GetGuestOutputPaintEffectForCvarValue instead as the
  // default fallback may be used for multiple values.
  static const char* GetCvarValueForGuestOutputPaintEffect(
      ui::Presenter::GuestOutputPaintConfig::Effect effect);
  static ui::Presenter::GuestOutputPaintConfig::Effect
  GetGuestOutputPaintEffectForCvarValue(const std::string& cvar_value);
  static ui::Presenter::GuestOutputPaintConfig
  GetGuestOutputPaintConfigForCvars();
  void ApplyDisplayConfigForCvars();

  void OnKeyDown(ui::KeyEvent& e);
  void OnMouseDown(const ui::MouseEvent& e);
  void OnMouseDoubleClick(const ui::MouseEvent& e);
  void FileDrop(const std::filesystem::path& filename);
  void OnMouseUp(const ui::MouseEvent& e);
  void FileClose();
  void InstallContent();
  void ExtractZarchive();
  void CreateZarchive();
  void ShowContentDirectory();
  void CpuTimeScalarReset();
  void CpuTimeScalarSetHalf();
  void CpuTimeScalarSetDouble();
  void CpuBreakIntoDebugger();
  void CpuBreakIntoHostDebugger();
  void GpuTraceFrame();
  void GpuClearCaches();
  void ToggleDisplayConfigDialog();
  void ShowCompatibility();
  void ShowFAQ();
  void ShowBuildCommit();
  void ShowAbout();

  EmulatorWindow::ControllerHotKey ProcessControllerHotkey(int buttons);
  void VibrateController(xe::hid::InputSystem* input_sys, uint32_t user_index,
                         bool vibrate = true);
  void GamepadHotKeys();
  void ToggleGPUSetting(gpu::GPUSetting setting);
  void CycleReadbackResolve();
  void DisplayHotKeysConfig();

  static std::string CanonicalizeFileExtension(
      const std::filesystem::path& path);

  void RunPreviouslyPlayedTitle();
  void FillRecentlyLaunchedTitlesMenu(xe::ui::MenuItem* recent_menu);
  void LoadRecentlyLaunchedTitles();

  // Get initial directory for file pickers based on recently played games
  std::filesystem::path GetFilePickerInitialDirectory() const;

  void ClearDialogs();

  // Timer callback for saving window size after resize is complete
  void SaveWindowSizeConfig();

  Emulator* emulator_;
  ui::WindowedAppContext& app_context_;
  bool is_game_process_;
  EmulatorWindowListener window_listener_;

  // Timer for debouncing resize events (save config after resize is done)
  std::unique_ptr<QTimer> resize_save_timer_;
  uint32_t pending_resize_width_ = 0;
  uint32_t pending_resize_height_ = 0;

#if XE_PLATFORM_LINUX
  std::vector<pid_t> child_processes_;
#elif XE_PLATFORM_WIN32
  std::vector<HANDLE> child_processes_;
#endif
  std::unique_ptr<ui::Window> window_;
  std::unique_ptr<ui::ImGuiDrawer> imgui_drawer_;
  // Creation may fail, in this case immediate drawer UI must not be drawn.
  std::unique_ptr<ui::ImmediateDrawer> immediate_drawer_;

  bool emulator_initialized_ = false;
  std::atomic<bool> disable_hotkeys_ = false;

  std::string base_title_;
  bool initializing_shader_storage_ = false;

  QPointer<class PostProcessingDialogQt> postprocessing_dialog_qt_;
  QPointer<class GameListDialogQt> game_list_dialog_qt_;
  QPointer<class ProfileDialogQt> profile_dialog_qt_;
  QPointer<class SimpleConfigDialogQt> simple_config_dialog_qt_;
  QPointer<class ConfigDialogQt> config_dialog_qt_;
  QPointer<class ContextMenuWidgetQt> context_menu_widget_qt_;
  QPointer<class XmpDialogQt> xmp_dialog_qt_;

  std::vector<RecentTitleEntry> recently_launched_titles_;

  // Menu items that need to be enabled/disabled based on child process state
  ui::MenuItem* file_menu_ = nullptr;
  ui::MenuItem* file_open_item_ = nullptr;
  ui::MenuItem* file_open_recent_menu_ = nullptr;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_EMULATOR_WINDOW_H_
