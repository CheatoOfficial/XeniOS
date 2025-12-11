/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_POSTPROCESSING_DIALOG_QT_H_
#define XENIA_UI_POSTPROCESSING_DIALOG_QT_H_

#include <QButtonGroup>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QVBoxLayout>

#include "xenia/gpu/command_processor.h"
#include "xenia/ui/gamepad_dialog_qt.h"
#include "xenia/ui/presenter.h"

namespace xe {
namespace app {

class EmulatorWindow;

class PostProcessingDialogQt : public ui::GamepadDialog {
  Q_OBJECT

 public:
  PostProcessingDialogQt(QWidget* parent, EmulatorWindow* emulator_window,
                         hid::InputSystem* input_system);
  ~PostProcessingDialogQt() override;

 private slots:
  void OnAntiAliasingChanged(int index);
  void OnResamplingEffectChanged(int index);
  void OnFsrSharpnessChanged(int value);
  void OnCasSharpnessChanged(int value);
  void OnFsrMaxUpsamplingPassesChanged(int value);
  void OnDitherChanged(int state);
  void OnResetFsrSharpness();
  void OnResetCasSharpness();
  void OnResetFsrMaxUpsamplingPasses();

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;

 private:
  void SetupUI();
  void LoadCurrentSettings();
  void UpdateEffectDescription();
  void UpdateSharpnessControls();

  EmulatorWindow* emulator_window_;
  QPoint drag_position_;

  // Anti-aliasing widgets
  QGroupBox* aa_group_;
  QButtonGroup* aa_button_group_;
  QRadioButton* aa_none_radio_;
  QRadioButton* aa_fxaa_radio_;
  QRadioButton* aa_fxaa_extreme_radio_;

  // Resampling and sharpening widgets
  QGroupBox* resampling_group_;
  QButtonGroup* resampling_button_group_;
  QRadioButton* effect_bilinear_radio_;
  QRadioButton* effect_cas_radio_;
  QRadioButton* effect_fsr_radio_;

  QLabel* effect_description_label_;
  QLabel* fxaa_recommendation_label_;

  // FSR sharpness widgets
  QWidget* fsr_sharpness_widget_;
  QLabel* fsr_sharpness_label_;
  QSlider* fsr_sharpness_slider_;
  QLabel* fsr_sharpness_value_label_;
  QPushButton* fsr_reset_button_;

  // CAS sharpness widgets
  QWidget* cas_sharpness_widget_;
  QLabel* cas_sharpness_label_;
  QSlider* cas_sharpness_slider_;
  QLabel* cas_sharpness_value_label_;
  QPushButton* cas_reset_button_;

  // FSR max upsampling passes widgets
  QWidget* fsr_max_upsampling_widget_;
  QLabel* fsr_max_upsampling_label_;
  QButtonGroup* fsr_max_upsampling_button_group_;
  QRadioButton* fsr_max_upsampling_radio_1_;
  QRadioButton* fsr_max_upsampling_radio_2_;
  QRadioButton* fsr_max_upsampling_radio_3_;
  QRadioButton* fsr_max_upsampling_radio_4_;
  QPushButton* fsr_max_upsampling_reset_button_;

  // Dithering widgets
  QGroupBox* dither_group_;
  QCheckBox* dither_checkbox_;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_POSTPROCESSING_DIALOG_QT_H_
