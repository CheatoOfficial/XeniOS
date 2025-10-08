/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/postprocessing_dialog_qt.h"

#include <QApplication>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QStyle>
#include <QVBoxLayout>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/app/emulator_window.h"
#include "xenia/base/cvar.h"
#include "xenia/gpu/graphics_system.h"

DECLARE_bool(postprocess_dither);
DECLARE_double(postprocess_ffx_cas_additional_sharpness);
DECLARE_double(postprocess_ffx_fsr_sharpness_reduction);
DECLARE_string(postprocess_antialiasing);
DECLARE_string(postprocess_scaling_and_sharpening);

namespace xe {
namespace app {

PostProcessingDialogQt::PostProcessingDialogQt(QWidget* parent,
                                               EmulatorWindow* emulator_window)
    : QDialog(parent), emulator_window_(emulator_window) {
  SetupUI();
  LoadCurrentSettings();

  // Position near top, centered horizontally
  if (parent) {
    QPoint parent_pos = parent->mapToGlobal(QPoint(0, 0));
    int center_x = parent_pos.x() + (parent->width() - width()) / 2;
    move(center_x, parent_pos.y() + 20);
  }
}

PostProcessingDialogQt::~PostProcessingDialogQt() = default;

void PostProcessingDialogQt::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    drag_position_ = event->position().toPoint();
    event->accept();
  }
}

void PostProcessingDialogQt::mouseMoveEvent(QMouseEvent* event) {
  if (event->buttons() & Qt::LeftButton) {
    QPoint global_pos = event->globalPosition().toPoint();
    move(global_pos - drag_position_);
    event->accept();
  }
}

void PostProcessingDialogQt::SetupUI() {
  setWindowTitle("Post-processing");
  setModal(false);
  setAttribute(Qt::WA_DeleteOnClose);
  setWindowFlags(Qt::Tool | Qt::FramelessWindowHint);
  setMinimumWidth(500);

  // Set window opacity (0.92 = 92% opaque)
  setWindowOpacity(0.92);

  // Set semi-transparent background similar to ImGui
  setStyleSheet(R"(
    QDialog {
      background-color: rgb(30, 30, 30);
      border: 1px solid rgba(100, 100, 100, 180);
      border-radius: 8px;
      padding: 0px;
    }
    QGroupBox {
      background-color: rgba(40, 40, 40, 200);
      border: 1px solid rgba(80, 80, 80, 150);
      border-radius: 6px;
      margin-top: 12px;
      padding-top: 16px;
      font-weight: bold;
      color: #e0e0e0;
    }
    QGroupBox::title {
      subcontrol-origin: margin;
      subcontrol-position: top left;
      padding: 4px 8px;
      color: #f0f0f0;
    }
    QLabel {
      color: #d0d0d0;
      background-color: transparent;
    }
    QRadioButton {
      color: #d0d0d0;
      background-color: transparent;
      spacing: 8px;
    }
    QRadioButton::indicator {
      width: 16px;
      height: 16px;
      border-radius: 8px;
      border: 2px solid #707070;
      background-color: rgba(50, 50, 50, 200);
    }
    QRadioButton::indicator:hover {
      border-color: #909090;
    }
    QRadioButton::indicator:checked {
      background-color: #4a9eff;
      border-color: #4a9eff;
    }
    QCheckBox {
      color: #d0d0d0;
      background-color: transparent;
      spacing: 8px;
    }
    QCheckBox::indicator {
      width: 16px;
      height: 16px;
      border-radius: 3px;
      border: 2px solid #707070;
      background-color: rgba(50, 50, 50, 200);
    }
    QCheckBox::indicator:hover {
      border-color: #909090;
    }
    QCheckBox::indicator:checked {
      background-color: #4a9eff;
      border-color: #4a9eff;
    }
    QSlider::groove:horizontal {
      height: 4px;
      background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                  stop:0 rgba(60, 60, 60, 200),
                                  stop:1 rgba(90, 90, 90, 200));
      border: 1px solid rgba(40, 40, 40, 150);
      border-radius: 2px;
    }
    QSlider::sub-page:horizontal {
      background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                                  stop:0 #3a7bc8,
                                  stop:1 #4a9eff);
      border-radius: 2px;
    }
    QSlider::handle:horizontal {
      background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                  stop:0 #6ac0ff,
                                  stop:1 #4a9eff);
      border: 2px solid rgba(255, 255, 255, 80);
      width: 14px;
      height: 14px;
      margin: -6px 0;
      border-radius: 7px;
    }
    QSlider::handle:horizontal:hover {
      background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                  stop:0 #7ad0ff,
                                  stop:1 #5ab0ff);
      border-color: rgba(255, 255, 255, 120);
    }
    QSlider::handle:horizontal:pressed {
      background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                                  stop:0 #5ab0ff,
                                  stop:1 #3a8fef);
    }
    QPushButton {
      background-color: rgba(70, 70, 70, 200);
      color: #e0e0e0;
      border: 1px solid rgba(100, 100, 100, 150);
      border-radius: 4px;
      padding: 6px 16px;
      font-weight: bold;
    }
    QPushButton:hover {
      background-color: rgba(90, 90, 90, 220);
      border-color: rgba(120, 120, 120, 180);
    }
    QPushButton:pressed {
      background-color: rgba(50, 50, 50, 220);
    }
  )");

  auto* content_layout = new QVBoxLayout(this);
  content_layout->setContentsMargins(16, 16, 16, 16);
  content_layout->setSpacing(12);

  // Top bar with close button
  auto* top_bar_layout = new QHBoxLayout();
  auto* info_label =
      new QLabel("All effects can be used on GPUs of any brand.", this);
  info_label->setWordWrap(true);
  info_label->setStyleSheet("color: #b0b0b0; font-style: italic;");
  top_bar_layout->addWidget(info_label);

  auto* close_button = new QPushButton(this);
  close_button->setIcon(style()->standardIcon(QStyle::SP_TitleBarCloseButton));
  close_button->setStyleSheet(R"(
    QPushButton {
      background-color: rgba(70, 70, 70, 200);
      border: 1px solid rgba(100, 100, 100, 150);
      border-radius: 12px;
      min-width: 24px;
      max-width: 24px;
      min-height: 24px;
      max-height: 24px;
      padding: 0px;
    }
    QPushButton:hover {
      background-color: rgba(200, 50, 50, 220);
      border-color: rgba(200, 50, 50, 255);
    }
    QPushButton:pressed {
      background-color: rgba(150, 30, 30, 220);
    }
  )");
  close_button->setToolTip("Close (F6)");
  connect(close_button, &QPushButton::clicked, this, &QDialog::close);
  top_bar_layout->addWidget(close_button, 0, Qt::AlignTop);

  content_layout->addLayout(top_bar_layout);

  content_layout->addSpacing(6);

  // Anti-aliasing group
  aa_group_ = new QGroupBox("Anti-aliasing", this);
  auto* aa_layout = new QVBoxLayout(aa_group_);
  aa_layout->setSpacing(6);

  aa_button_group_ = new QButtonGroup(this);
  aa_none_radio_ = new QRadioButton("None", aa_group_);
  aa_fxaa_radio_ = new QRadioButton(
      "NVIDIA Fast Approximate Anti-Aliasing (FXAA) [Normal Quality]",
      aa_group_);
  aa_fxaa_extreme_radio_ = new QRadioButton(
      "NVIDIA Fast Approximate Anti-Aliasing (FXAA) [Extreme Quality]",
      aa_group_);

  aa_button_group_->addButton(aa_none_radio_, 0);
  aa_button_group_->addButton(aa_fxaa_radio_, 1);
  aa_button_group_->addButton(aa_fxaa_extreme_radio_, 2);

  aa_layout->addWidget(aa_none_radio_);
  aa_layout->addWidget(aa_fxaa_radio_);
  aa_layout->addWidget(aa_fxaa_extreme_radio_);

  connect(aa_button_group_, QOverload<int>::of(&QButtonGroup::idClicked), this,
          &PostProcessingDialogQt::OnAntiAliasingChanged);

  content_layout->addWidget(aa_group_);

  // Resampling and sharpening group
  resampling_group_ = new QGroupBox("Resampling and sharpening", this);
  auto* resampling_layout = new QVBoxLayout(resampling_group_);
  resampling_layout->setSpacing(8);

  resampling_button_group_ = new QButtonGroup(this);
  effect_bilinear_radio_ =
      new QRadioButton("None / Bilinear", resampling_group_);
  effect_cas_radio_ = new QRadioButton(
      "AMD FidelityFX Contrast Adaptive Sharpening (CAS)", resampling_group_);
  effect_fsr_radio_ = new QRadioButton(
      "AMD FidelityFX Super Resolution 1.0 (FSR)", resampling_group_);

  resampling_button_group_->addButton(
      effect_bilinear_radio_,
      static_cast<int>(
          ui::Presenter::GuestOutputPaintConfig::Effect::kBilinear));
  resampling_button_group_->addButton(
      effect_cas_radio_,
      static_cast<int>(ui::Presenter::GuestOutputPaintConfig::Effect::kCas));
  resampling_button_group_->addButton(
      effect_fsr_radio_,
      static_cast<int>(ui::Presenter::GuestOutputPaintConfig::Effect::kFsr));

  resampling_layout->addWidget(effect_bilinear_radio_);
  resampling_layout->addWidget(effect_cas_radio_);
  resampling_layout->addWidget(effect_fsr_radio_);

  connect(resampling_button_group_,
          QOverload<int>::of(&QButtonGroup::idClicked), this,
          &PostProcessingDialogQt::OnResamplingEffectChanged);

  // Effect description
  effect_description_label_ = new QLabel(resampling_group_);
  effect_description_label_->setWordWrap(true);
  effect_description_label_->setStyleSheet(
      "QLabel { color: #999; margin-top: 8px; font-size: 11px; }");
  resampling_layout->addWidget(effect_description_label_);

  // FXAA recommendation label
  fxaa_recommendation_label_ = new QLabel(
      "FXAA is highly recommended when using CAS or FSR.", resampling_group_);
  fxaa_recommendation_label_->setWordWrap(true);
  fxaa_recommendation_label_->setStyleSheet(
      "QLabel { margin-top: 10px; color: #aad4ff; font-weight: bold; }");
  resampling_layout->addWidget(fxaa_recommendation_label_);

  // FSR sharpness controls
  fsr_sharpness_widget_ = new QWidget(resampling_group_);
  auto* fsr_layout = new QVBoxLayout(fsr_sharpness_widget_);
  fsr_layout->setContentsMargins(0, 10, 0, 0);

  fsr_sharpness_label_ =
      new QLabel("FSR sharpness reduction when upscaling (lower is sharper):",
                 fsr_sharpness_widget_);
  fsr_layout->addWidget(fsr_sharpness_label_);

  auto* fsr_slider_layout = new QHBoxLayout();
  fsr_sharpness_slider_ = new QSlider(Qt::Horizontal, fsr_sharpness_widget_);
  fsr_sharpness_slider_->setRange(0, 200);  // Will be mapped to 0.0-2.0
  fsr_sharpness_value_label_ = new QLabel("0%", fsr_sharpness_widget_);
  fsr_sharpness_value_label_->setMinimumWidth(50);
  fsr_reset_button_ = new QPushButton("Reset", fsr_sharpness_widget_);

  fsr_slider_layout->addWidget(fsr_sharpness_slider_);
  fsr_slider_layout->addWidget(fsr_sharpness_value_label_);
  fsr_slider_layout->addWidget(fsr_reset_button_);
  fsr_layout->addLayout(fsr_slider_layout);

  connect(fsr_sharpness_slider_, &QSlider::valueChanged, this,
          &PostProcessingDialogQt::OnFsrSharpnessChanged);
  connect(fsr_reset_button_, &QPushButton::clicked, this,
          &PostProcessingDialogQt::OnResetFsrSharpness);

  resampling_layout->addWidget(fsr_sharpness_widget_);

  // CAS sharpness controls
  cas_sharpness_widget_ = new QWidget(resampling_group_);
  auto* cas_layout = new QVBoxLayout(cas_sharpness_widget_);
  cas_layout->setContentsMargins(0, 10, 0, 0);

  cas_sharpness_label_ = new QLabel(
      "CAS additional sharpness (higher is sharper):", cas_sharpness_widget_);
  cas_layout->addWidget(cas_sharpness_label_);

  auto* cas_slider_layout = new QHBoxLayout();
  cas_sharpness_slider_ = new QSlider(Qt::Horizontal, cas_sharpness_widget_);
  cas_sharpness_slider_->setRange(0, 100);  // Will be mapped to 0.0-1.0
  cas_sharpness_value_label_ = new QLabel("0%", cas_sharpness_widget_);
  cas_sharpness_value_label_->setMinimumWidth(50);
  cas_reset_button_ = new QPushButton("Reset", cas_sharpness_widget_);

  cas_slider_layout->addWidget(cas_sharpness_slider_);
  cas_slider_layout->addWidget(cas_sharpness_value_label_);
  cas_slider_layout->addWidget(cas_reset_button_);
  cas_layout->addLayout(cas_slider_layout);

  connect(cas_sharpness_slider_, &QSlider::valueChanged, this,
          &PostProcessingDialogQt::OnCasSharpnessChanged);
  connect(cas_reset_button_, &QPushButton::clicked, this,
          &PostProcessingDialogQt::OnResetCasSharpness);

  resampling_layout->addWidget(cas_sharpness_widget_);

  content_layout->addWidget(resampling_group_);

  // Dithering group
  dither_group_ = new QGroupBox("Dithering", this);
  auto* dither_layout = new QVBoxLayout(dither_group_);
  dither_layout->setSpacing(6);

  dither_checkbox_ = new QCheckBox(
      "Dither the final output to 8bpc to make gradients smoother",
      dither_group_);
  dither_layout->addWidget(dither_checkbox_);

  connect(dither_checkbox_, &QCheckBox::checkStateChanged, this,
          &PostProcessingDialogQt::OnDitherChanged);

  content_layout->addWidget(dither_group_);

  content_layout->addStretch();
}

void PostProcessingDialogQt::LoadCurrentSettings() {
  auto emulator = emulator_window_->emulator();
  if (!emulator) {
    return;
  }

  auto graphics_system = emulator->graphics_system();
  if (!graphics_system) {
    return;
  }

  // Load anti-aliasing settings
  auto command_processor = graphics_system->command_processor();
  if (command_processor) {
    auto current_aa = command_processor->GetDesiredSwapPostEffect();
    aa_button_group_->button(static_cast<int>(current_aa))->setChecked(true);
  }

  // Load resampling and sharpening settings
  auto presenter = graphics_system->presenter();
  if (presenter) {
    const auto& config = presenter->GetGuestOutputPaintConfigFromUIThread();

    // Set effect radio button
    resampling_button_group_->button(static_cast<int>(config.GetEffect()))
        ->setChecked(true);

    // Set FSR sharpness (convert from 0.0-2.0 to 0-200)
    float fsr_sharpness = config.GetFsrSharpnessReduction();
    // Apply power 2.0 scaling as done in ImGui version
    fsr_sharpness = sqrt(2.f * fsr_sharpness);
    fsr_sharpness_slider_->setValue(static_cast<int>(fsr_sharpness * 100));

    // Set CAS sharpness (convert from 0.0-1.0 to 0-100)
    float cas_sharpness = config.GetCasAdditionalSharpness();
    cas_sharpness_slider_->setValue(static_cast<int>(cas_sharpness * 100));

    // Set dithering
    dither_checkbox_->setChecked(config.GetDither());
  }

  UpdateEffectDescription();
  UpdateSharpnessControls();
}

void PostProcessingDialogQt::OnAntiAliasingChanged(int index) {
  auto emulator = emulator_window_->emulator();
  if (!emulator) {
    return;
  }

  auto graphics_system = emulator->graphics_system();
  if (!graphics_system) {
    return;
  }

  auto command_processor = graphics_system->command_processor();
  if (!command_processor) {
    return;
  }

  auto new_effect = static_cast<gpu::CommandProcessor::SwapPostEffect>(index);
  command_processor->SetDesiredSwapPostEffect(new_effect);

  // Update cvar
  emulator_window_->UpdateAntiAliasingCvar(new_effect);
}

void PostProcessingDialogQt::OnResamplingEffectChanged(int index) {
  auto emulator = emulator_window_->emulator();
  if (!emulator) {
    return;
  }

  auto graphics_system = emulator->graphics_system();
  if (!graphics_system) {
    return;
  }

  auto presenter = graphics_system->presenter();
  if (!presenter) {
    return;
  }

  auto config = presenter->GetGuestOutputPaintConfigFromUIThread();
  auto new_effect =
      static_cast<ui::Presenter::GuestOutputPaintConfig::Effect>(index);
  config.SetEffect(new_effect);
  presenter->SetGuestOutputPaintConfigFromUIThread(config);

  // Update cvar
  emulator_window_->UpdateScalingAndSharpeningCvar(new_effect);

  UpdateEffectDescription();
  UpdateSharpnessControls();
}

void PostProcessingDialogQt::OnFsrSharpnessChanged(int value) {
  auto emulator = emulator_window_->emulator();
  if (!emulator) {
    return;
  }

  auto graphics_system = emulator->graphics_system();
  if (!graphics_system) {
    return;
  }

  auto presenter = graphics_system->presenter();
  if (!presenter) {
    return;
  }

  // Convert slider value (0-200) to FSR sharpness (0.0-2.0) with power 2.0
  // scaling
  float slider_value = value / 100.0f;
  float fsr_sharpness = 0.5f * slider_value * slider_value;

  auto config = presenter->GetGuestOutputPaintConfigFromUIThread();
  config.SetFsrSharpnessReduction(fsr_sharpness);
  presenter->SetGuestOutputPaintConfigFromUIThread(config);

  // Update label
  fsr_sharpness_value_label_->setText(QString::fromStdString(
      fmt::format("{} %", static_cast<int>(fsr_sharpness * 100))));

  // Update cvar
  emulator_window_->UpdateFsrSharpnessCvar(fsr_sharpness);
}

void PostProcessingDialogQt::OnCasSharpnessChanged(int value) {
  auto emulator = emulator_window_->emulator();
  if (!emulator) {
    return;
  }

  auto graphics_system = emulator->graphics_system();
  if (!graphics_system) {
    return;
  }

  auto presenter = graphics_system->presenter();
  if (!presenter) {
    return;
  }

  // Convert slider value (0-100) to CAS sharpness (0.0-1.0)
  float cas_sharpness = value / 100.0f;

  auto config = presenter->GetGuestOutputPaintConfigFromUIThread();
  config.SetCasAdditionalSharpness(cas_sharpness);
  presenter->SetGuestOutputPaintConfigFromUIThread(config);

  // Update label
  cas_sharpness_value_label_->setText(QString::fromStdString(
      fmt::format("{} %", static_cast<int>(cas_sharpness * 100))));

  // Update cvar
  emulator_window_->UpdateCasSharpnessCvar(cas_sharpness);
}

void PostProcessingDialogQt::OnDitherChanged(int state) {
  auto emulator = emulator_window_->emulator();
  if (!emulator) {
    return;
  }

  auto graphics_system = emulator->graphics_system();
  if (!graphics_system) {
    return;
  }

  auto presenter = graphics_system->presenter();
  if (!presenter) {
    return;
  }

  bool dither = (state == Qt::Checked);

  auto config = presenter->GetGuestOutputPaintConfigFromUIThread();
  config.SetDither(dither);
  presenter->SetGuestOutputPaintConfigFromUIThread(config);

  // Update cvar
  emulator_window_->UpdateDitherCvar(dither);
}

void PostProcessingDialogQt::OnResetFsrSharpness() {
  float default_value =
      ui::Presenter::GuestOutputPaintConfig::kFsrSharpnessReductionDefault;

  // Apply power 2.0 scaling
  float slider_value = sqrt(2.f * default_value);
  fsr_sharpness_slider_->setValue(static_cast<int>(slider_value * 100));
}

void PostProcessingDialogQt::OnResetCasSharpness() {
  float default_value =
      ui::Presenter::GuestOutputPaintConfig::kCasAdditionalSharpnessDefault;
  cas_sharpness_slider_->setValue(static_cast<int>(default_value * 100));
}

void PostProcessingDialogQt::UpdateEffectDescription() {
  auto emulator = emulator_window_->emulator();
  if (!emulator) {
    return;
  }

  auto graphics_system = emulator->graphics_system();
  if (!graphics_system) {
    return;
  }

  auto presenter = graphics_system->presenter();
  if (!presenter) {
    return;
  }

  const auto& config = presenter->GetGuestOutputPaintConfigFromUIThread();

  const char* description = nullptr;
  switch (config.GetEffect()) {
    case ui::Presenter::GuestOutputPaintConfig::Effect::kBilinear:
      description =
          "Simple bilinear filtering is done if resampling is needed.\n"
          "Otherwise, only anti-aliasing is done if enabled, or displaying as "
          "is.";
      break;
    case ui::Presenter::GuestOutputPaintConfig::Effect::kCas:
      description =
          "Sharpening and resampling to up to 2x2 to improve the fidelity of "
          "details.\n"
          "For scaling by more than 2x2, bilinear stretching is done "
          "afterwards.";
      break;
    case ui::Presenter::GuestOutputPaintConfig::Effect::kFsr:
      description =
          "High-quality edge-preserving upscaling to arbitrary target "
          "resolutions.\n"
          "For scaling by more than 2x2, multiple upsampling passes are done.\n"
          "If not upscaling, Contrast Adaptive Sharpening (CAS) is used "
          "instead.";
      break;
  }

  if (description) {
    effect_description_label_->setText(description);
    effect_description_label_->setVisible(true);
  } else {
    effect_description_label_->setVisible(false);
  }
}

void PostProcessingDialogQt::UpdateSharpnessControls() {
  auto emulator = emulator_window_->emulator();
  if (!emulator) {
    return;
  }

  auto graphics_system = emulator->graphics_system();
  if (!graphics_system) {
    return;
  }

  auto presenter = graphics_system->presenter();
  if (!presenter) {
    return;
  }

  const auto& config = presenter->GetGuestOutputPaintConfigFromUIThread();

  bool is_cas =
      config.GetEffect() == ui::Presenter::GuestOutputPaintConfig::Effect::kCas;
  bool is_fsr =
      config.GetEffect() == ui::Presenter::GuestOutputPaintConfig::Effect::kFsr;

  // Show/hide FXAA recommendation
  fxaa_recommendation_label_->setVisible(is_cas || is_fsr);

  // Show/hide FSR sharpness controls
  fsr_sharpness_widget_->setVisible(is_fsr);

  // Show/hide CAS sharpness controls
  cas_sharpness_widget_->setVisible(is_cas || is_fsr);

  // Update CAS label text based on effect
  if (is_fsr) {
    cas_sharpness_label_->setText(
        "CAS additional sharpness when not upscaling (higher is sharper):");
  } else {
    cas_sharpness_label_->setText(
        "CAS additional sharpness (higher is sharper):");
  }

  // Resize dialog to fit content after showing/hiding widgets
  adjustSize();
}

}  // namespace app
}  // namespace xe
