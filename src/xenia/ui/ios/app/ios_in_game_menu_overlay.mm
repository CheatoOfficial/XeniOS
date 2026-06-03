/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#import "xenia/ui/ios/app/ios_in_game_menu_overlay.h"

#import "xenia/ui/ios/shared/ios_theme.h"
#import "xenia/ui/ios/shared/ios_view_helpers.h"

@implementation XeniaIOSInGameMenuOverlay {
  UIView* _panel;
  UIButton* _resumeButton;
  UIButton* _editControlsButton;
  UIButton* _achievementsButton;
  UIButton* _displayButton;
  UIButton* _settingsButton;
  UIButton* _graphicsButton;
  UIButton* _liveLogButton;
  UIButton* _exitButton;
  UIMenu* _displayMenu;
  NSLayoutConstraint* _panelWidthConstraint;
  BOOL _controllerNavigationEnabled;
  XeniaIOSInGameMenuAction _focusedAction;
  void (^_graphicsHandler)(void);
}

- (instancetype)initWithFrame:(CGRect)frame {
  if (!(self = [super initWithFrame:frame])) {
    return nil;
  }

  self.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
  self.backgroundColor = [XeniaTheme overlayLight];
  self.hidden = YES;
  self.userInteractionEnabled = NO;
  _focusedAction = XeniaIOSInGameMenuActionNone;
  _controllerNavigationEnabled = NO;

  _panel = [[UIView alloc] init];
  _panel.translatesAutoresizingMaskIntoConstraints = NO;
  xe_apply_floating_window_chrome(_panel);
  [self addSubview:_panel];

  UILabel* title = [[UILabel alloc] init];
  title.translatesAutoresizingMaskIntoConstraints = NO;
  title.text = @"In-Game Menu";
  title.textColor = [XeniaTheme textPrimary];
  xe_apply_label_font(title, UIFontTextStyleTitle2, 22.0, UIFontWeightSemibold);
  title.textAlignment = NSTextAlignmentCenter;
  title.accessibilityTraits = UIAccessibilityTraitHeader;
  [_panel addSubview:title];

  UILabel* subtitle = [[UILabel alloc] init];
  subtitle.translatesAutoresizingMaskIntoConstraints = NO;
  subtitle.text = @"Tap anywhere to close";
  subtitle.textColor = [XeniaTheme textMuted];
  xe_apply_label_font(subtitle, UIFontTextStyleSubheadline, 15.0, UIFontWeightRegular);
  subtitle.textAlignment = NSTextAlignmentCenter;
  [_panel addSubview:subtitle];

  _resumeButton = [self newButtonWithTitle:@"Resume"
                                  imageName:nil
                           backgroundColor:[XeniaTheme accent]
                            foregroundColor:[XeniaTheme accentFg]
                                     action:@selector(resumePressed:)];
  [_panel addSubview:_resumeButton];

  _editControlsButton = [self newButtonWithTitle:@"Edit Controls"
                                       imageName:@"hand.tap"
                                backgroundColor:[XeniaTheme bgSurface2]
                                 foregroundColor:[XeniaTheme textPrimary]
                                          action:@selector(editControlsPressed:)];
  [_panel addSubview:_editControlsButton];

  _achievementsButton = [self newButtonWithTitle:@"Achievements"
                                        imageName:@"trophy"
                                 backgroundColor:[XeniaTheme bgSurface2]
                                  foregroundColor:[XeniaTheme textPrimary]
                                           action:@selector(achievementsPressed:)];
  [_panel addSubview:_achievementsButton];

  _displayButton = [self newButtonWithTitle:@"Display"
                                 imageName:@"rectangle.expand.vertical"
                          backgroundColor:[XeniaTheme bgSurface2]
                           foregroundColor:[XeniaTheme textPrimary]
                                    action:nil];
  _displayButton.showsMenuAsPrimaryAction = YES;
  [_panel addSubview:_displayButton];

  _settingsButton = [self newButtonWithTitle:@"Settings"
                                   imageName:@"slider.horizontal.3"
                            backgroundColor:[XeniaTheme bgSurface2]
                             foregroundColor:[XeniaTheme textPrimary]
                                      action:@selector(settingsPressed:)];
  [_panel addSubview:_settingsButton];

  _graphicsButton = [self newButtonWithTitle:@"Graphics"
                                   imageName:@"gearshape.2.fill"
                            backgroundColor:[XeniaTheme bgSurface2]
                             foregroundColor:[XeniaTheme textPrimary]
                                      action:@selector(graphicsPressed:)];
  [_panel addSubview:_graphicsButton];

  _liveLogButton = [self newButtonWithTitle:@"Live Log"
                                  imageName:@"doc.text"
                           backgroundColor:[XeniaTheme bgSurface2]
                            foregroundColor:[XeniaTheme textPrimary]
                                     action:@selector(liveLogPressed:)];
  [_panel addSubview:_liveLogButton];

  _exitButton = [self newButtonWithTitle:@"Exit To Library"
                               imageName:@"rectangle.portrait.and.arrow.right"
                        backgroundColor:[[XeniaTheme statusError] colorWithAlphaComponent:0.25]
                         foregroundColor:[XeniaTheme textPrimary]
                                  action:@selector(exitPressed:)];
  [_panel addSubview:_exitButton];

  [NSLayoutConstraint activateConstraints:@[
    [_panel.centerXAnchor constraintEqualToAnchor:self.centerXAnchor],
    [_panel.centerYAnchor constraintEqualToAnchor:self.centerYAnchor],
    [_panel.leadingAnchor constraintGreaterThanOrEqualToAnchor:self.safeAreaLayoutGuide.leadingAnchor
                                                      constant:24],
    [_panel.trailingAnchor constraintLessThanOrEqualToAnchor:self.safeAreaLayoutGuide.trailingAnchor
                                                    constant:-24],

    [title.topAnchor constraintEqualToAnchor:_panel.topAnchor constant:18],
    [title.leadingAnchor constraintEqualToAnchor:_panel.leadingAnchor constant:20],
    [title.trailingAnchor constraintEqualToAnchor:_panel.trailingAnchor constant:-20],

    [subtitle.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:4],
    [subtitle.leadingAnchor constraintEqualToAnchor:title.leadingAnchor],
    [subtitle.trailingAnchor constraintEqualToAnchor:title.trailingAnchor],
  ]];

  _panelWidthConstraint = [_panel.widthAnchor constraintLessThanOrEqualToConstant:420];
  [NSLayoutConstraint activateConstraints:@[
    _panelWidthConstraint,

    // Resume — full width.
    [_resumeButton.topAnchor constraintEqualToAnchor:subtitle.bottomAnchor constant:16],
    [_resumeButton.leadingAnchor constraintEqualToAnchor:_panel.leadingAnchor constant:14],
    [_resumeButton.trailingAnchor constraintEqualToAnchor:_panel.trailingAnchor constant:-14],

    // Row 1: Edit Controls | Graphics
    [_editControlsButton.topAnchor constraintEqualToAnchor:_resumeButton.bottomAnchor constant:10],
    [_editControlsButton.leadingAnchor constraintEqualToAnchor:_resumeButton.leadingAnchor],
    [_graphicsButton.topAnchor constraintEqualToAnchor:_editControlsButton.topAnchor],
    [_graphicsButton.leadingAnchor constraintEqualToAnchor:_editControlsButton.trailingAnchor
                                                  constant:10],
    [_graphicsButton.trailingAnchor constraintEqualToAnchor:_resumeButton.trailingAnchor],
    [_graphicsButton.widthAnchor constraintEqualToAnchor:_editControlsButton.widthAnchor],

    // Row 2: Display | Settings
    [_displayButton.topAnchor constraintEqualToAnchor:_editControlsButton.bottomAnchor constant:10],
    [_displayButton.leadingAnchor constraintEqualToAnchor:_resumeButton.leadingAnchor],
    [_settingsButton.topAnchor constraintEqualToAnchor:_displayButton.topAnchor],
    [_settingsButton.leadingAnchor constraintEqualToAnchor:_displayButton.trailingAnchor
                                                  constant:10],
    [_settingsButton.trailingAnchor constraintEqualToAnchor:_resumeButton.trailingAnchor],
    [_settingsButton.widthAnchor constraintEqualToAnchor:_displayButton.widthAnchor],

    // Row 3: Achievements | Exit
    [_achievementsButton.topAnchor constraintEqualToAnchor:_displayButton.bottomAnchor
                                                  constant:10],
    [_achievementsButton.leadingAnchor constraintEqualToAnchor:_resumeButton.leadingAnchor],
    [_exitButton.topAnchor constraintEqualToAnchor:_achievementsButton.topAnchor],
    [_exitButton.leadingAnchor constraintEqualToAnchor:_achievementsButton.trailingAnchor
                                              constant:10],
    [_exitButton.trailingAnchor constraintEqualToAnchor:_resumeButton.trailingAnchor],
    [_exitButton.widthAnchor constraintEqualToAnchor:_achievementsButton.widthAnchor],

    // Footer: Live Log
    [_liveLogButton.topAnchor constraintEqualToAnchor:_achievementsButton.bottomAnchor constant:10],
    [_liveLogButton.centerXAnchor constraintEqualToAnchor:_panel.centerXAnchor],
    [_liveLogButton.bottomAnchor constraintEqualToAnchor:_panel.bottomAnchor constant:-14],
  ]];
  [title release];
  [subtitle release];
  return self;
}

- (void)dealloc {
  [_resumeHandler release];
  [_editControlsHandler release];
  [_achievementsHandler release];
  [_settingsHandler release];
  [_liveLogHandler release];
  [_exitHandler release];
  [_graphicsHandler release];
  [_displayMenu release];
  [_panelWidthConstraint release];
  [_panel release];
  [_resumeButton release];
  [_editControlsButton release];
  [_achievementsButton release];
  [_displayButton release];
  [_settingsButton release];
  [_graphicsButton release];
  [_liveLogButton release];
  [_exitButton release];
  [super dealloc];
}

- (UIButton*)newButtonWithTitle:(NSString*)title
                      imageName:(NSString*)imageName
               backgroundColor:(UIColor*)backgroundColor
                foregroundColor:(UIColor*)foregroundColor
                         action:(SEL)action {
  UIButtonConfiguration* config = [UIButtonConfiguration tintedButtonConfiguration];
  config.title = title;
  if (imageName.length) {
    config.image = [UIImage systemImageNamed:imageName];
    config.imagePadding = 6;
  }
  config.baseForegroundColor = foregroundColor;
  config.baseBackgroundColor = backgroundColor;
  config.cornerStyle = UIButtonConfigurationCornerStyleLarge;
  config.contentInsets = NSDirectionalEdgeInsetsMake(10, 16, 10, 16);
  if ([title isEqualToString:@"Resume"]) {
    config = [UIButtonConfiguration filledButtonConfiguration];
    config.title = title;
    config.baseBackgroundColor = backgroundColor;
    config.baseForegroundColor = foregroundColor;
    config.cornerStyle = UIButtonConfigurationCornerStyleLarge;
    config.contentInsets = NSDirectionalEdgeInsetsMake(12, 18, 12, 18);
  }
  UIButton* button = [[UIButton buttonWithConfiguration:config primaryAction:nil] retain];
  button.translatesAutoresizingMaskIntoConstraints = NO;
  xe_apply_button_title_font(button, UIFontTextStyleBody, 16.0, UIFontWeightSemibold);
  button.accessibilityLabel = title;
  if ([title isEqualToString:@"Resume"]) {
    button.accessibilityHint = @"Returns to the game.";
  } else if ([title isEqualToString:@"Edit Controls"]) {
    button.accessibilityHint = @"Opens the touch control editor.";
  } else if ([title isEqualToString:@"Achievements"]) {
    button.accessibilityHint = @"Shows achievements for the current game.";
  } else if ([title isEqualToString:@"Display"]) {
    button.accessibilityHint = @"Opens display scaling and position options.";
  } else if ([title isEqualToString:@"Graphics"]) {
    button.accessibilityHint = @"Opens graphics compatibility settings.";
  } else if ([title isEqualToString:@"Settings"]) {
    button.accessibilityHint = @"Opens XeniOS settings.";
  } else if ([title isEqualToString:@"Live Log"]) {
    button.accessibilityHint = @"Opens the live emulator log.";
  } else if ([title isEqualToString:@"Exit To Library"]) {
    button.accessibilityHint = @"Stops the game and returns to the library.";
  }
  if (action) {
    [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
  }
  return button;
}

- (void)setDisplayMenu:(UIMenu*)displayMenu {
  if (_displayMenu == displayMenu) {
    return;
  }
  [_displayMenu release];
  _displayMenu = [displayMenu retain];
  _displayButton.menu = _displayMenu;
}

- (UIMenu*)displayMenu {
  return _displayMenu;
}

- (void)setGraphicsHandler:(void (^)(void))handler {
  if (_graphicsHandler == handler) return;
  [_graphicsHandler release];
  _graphicsHandler = [handler copy];
}

- (void (^)(void))graphicsHandler {
  return _graphicsHandler;
}

- (BOOL)isOverlayVisible {
  return !self.hidden;
}

- (UIButton*)buttonForAction:(XeniaIOSInGameMenuAction)action {
  switch (action) {
    case XeniaIOSInGameMenuActionResume:
      return _resumeButton;
    case XeniaIOSInGameMenuActionEditControls:
      return _editControlsButton;
    case XeniaIOSInGameMenuActionAchievements:
      return _achievementsButton;
    case XeniaIOSInGameMenuActionDisplay:
      return _displayButton;
    case XeniaIOSInGameMenuActionSettings:
      return _settingsButton;
    case XeniaIOSInGameMenuActionLiveLog:
      return _liveLogButton;
    case XeniaIOSInGameMenuActionExit:
      return _exitButton;
    case XeniaIOSInGameMenuActionGraphics:
      return _graphicsButton;
    case XeniaIOSInGameMenuActionNone:
    default:
      return nil;
  }
}

- (BOOL)isActionEnabled:(XeniaIOSInGameMenuAction)action {
  UIButton* button = [self buttonForAction:action];
  return button && button.enabled && !button.hidden;
}

- (void)performAction:(XeniaIOSInGameMenuAction)action {
  UIButton* button = [self buttonForAction:action];
  if (!button || !button.enabled || button.hidden) {
    return;
  }
  if (action == XeniaIOSInGameMenuActionDisplay) {
    if (@available(iOS 17.4, *)) {
      [button performPrimaryAction];
      return;
    }
  }
  [button sendActionsForControlEvents:UIControlEventTouchUpInside];
}

- (void)setButton:(UIButton*)button controllerFocused:(BOOL)focused {
  if (!button) {
    return;
  }
  button.layer.cornerRadius = XeniaRadiusMd;
  button.layer.borderWidth = focused ? 1.5 : 0.0;
  button.layer.borderColor = focused ? [XeniaTheme accent].CGColor : [UIColor clearColor].CGColor;
  button.layer.shadowColor = [XeniaTheme accent].CGColor;
  button.layer.shadowOpacity = focused ? 0.35f : 0.0f;
  button.layer.shadowRadius = focused ? 6.0f : 0.0f;
  button.layer.shadowOffset = CGSizeZero;
}

- (void)setControllerNavigationEnabled:(BOOL)enabled
                         focusedAction:(XeniaIOSInGameMenuAction)focusedAction {
  _controllerNavigationEnabled = enabled;
  _focusedAction = enabled ? focusedAction : XeniaIOSInGameMenuActionNone;

  [self setButton:_resumeButton
      controllerFocused:_controllerNavigationEnabled &&
                        _focusedAction == XeniaIOSInGameMenuActionResume];
  [self setButton:_editControlsButton
      controllerFocused:_controllerNavigationEnabled &&
                        _focusedAction == XeniaIOSInGameMenuActionEditControls];
  [self setButton:_achievementsButton
      controllerFocused:_controllerNavigationEnabled &&
                        _focusedAction == XeniaIOSInGameMenuActionAchievements];
  [self setButton:_displayButton
      controllerFocused:_controllerNavigationEnabled &&
                        _focusedAction == XeniaIOSInGameMenuActionDisplay];
  [self setButton:_settingsButton
      controllerFocused:_controllerNavigationEnabled &&
                        _focusedAction == XeniaIOSInGameMenuActionSettings];
  [self setButton:_liveLogButton
      controllerFocused:_controllerNavigationEnabled &&
                        _focusedAction == XeniaIOSInGameMenuActionLiveLog];
  [self setButton:_graphicsButton
      controllerFocused:_controllerNavigationEnabled &&
                        _focusedAction == XeniaIOSInGameMenuActionGraphics];
  [self setButton:_exitButton
      controllerFocused:_controllerNavigationEnabled &&
                        _focusedAction == XeniaIOSInGameMenuActionExit];
}

- (void)setOverlayVisible:(BOOL)visible
                 animated:(BOOL)animated
               completion:(void (^)(BOOL finished))completion {
  self.userInteractionEnabled = visible;
  if (visible == !self.hidden) {
    if (completion) {
      completion(YES);
    }
    return;
  }

  if (!animated) {
    self.hidden = !visible;
    self.alpha = visible ? 1.0 : 0.0;
    if (!visible) {
      self.alpha = 1.0;
    }
    if (completion) {
      completion(YES);
    }
    return;
  }

  if (visible) {
    self.alpha = 0.0;
    self.hidden = NO;
    [UIView animateWithDuration:0.18
        animations:^{
          self.alpha = 1.0;
        }
        completion:completion];
  } else {
    [UIView animateWithDuration:0.15
        animations:^{
          self.alpha = 0.0;
        }
        completion:^(BOOL finished) {
          self.hidden = YES;
          self.alpha = 1.0;
          if (completion) {
            completion(finished);
          }
        }];
  }
}

- (void)layoutSubviews {
  [super layoutSubviews];
  const BOOL isLandscape = self.bounds.size.width > self.bounds.size.height;
  _panelWidthConstraint.constant = isLandscape ? 540.0 : 420.0;
}

- (void)resumePressed:(UIButton*)__unused sender {
  if (_resumeHandler) {
    _resumeHandler();
  }
}

- (void)editControlsPressed:(UIButton*)__unused sender {
  if (_editControlsHandler) {
    _editControlsHandler();
  }
}

- (void)achievementsPressed:(UIButton*)__unused sender {
  if (_achievementsHandler) {
    _achievementsHandler();
  }
}

- (void)settingsPressed:(UIButton*)__unused sender {
  if (_settingsHandler) {
    _settingsHandler();
  }
}

- (void)liveLogPressed:(UIButton*)__unused sender {
  if (_liveLogHandler) {
    _liveLogHandler();
  }
}

- (void)graphicsPressed:(UIButton*)__unused sender {
  if (_graphicsHandler) {
    _graphicsHandler();
  }
}

- (void)exitPressed:(UIButton*)__unused sender {
  if (_exitHandler) {
    _exitHandler();
  }
}

@end
