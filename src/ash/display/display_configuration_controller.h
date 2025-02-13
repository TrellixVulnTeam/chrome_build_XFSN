// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_DISPLAY_DISPLAY_CONFIGURATION_CONTROLLER_H_
#define ASH_DISPLAY_DISPLAY_CONFIGURATION_CONTROLLER_H_

#include <memory>

#include "ash/ash_export.h"
#include "ash/display/window_tree_host_manager.h"
#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "ui/display/display.h"

namespace display {
class DisplayLayout;
class DisplayManager;
}

namespace ash {

class DisplayAnimator;
class ScreenRotationAnimator;

// This class controls Display related configuration. Specifically it:
// * Handles animated transitions where appropriate.
// * Limits the frequency of certain operations.
// * Provides a single interface for UI and API classes.
// * TODO: Forwards display configuration changed events to UI and API classes.
class ASH_EXPORT DisplayConfigurationController
    : public WindowTreeHostManager::Observer {
 public:
  DisplayConfigurationController(
      display::DisplayManager* display_manager,
      WindowTreeHostManager* window_tree_host_manager);
  ~DisplayConfigurationController() override;

  // Sets the layout for the current displays with a fade in/out
  // animation. Currently |display_id| is assumed to be the secondary
  // display.  TODO(oshima/stevenjb): Support 3+ displays.
  void SetDisplayLayout(std::unique_ptr<display::DisplayLayout> layout);

  // Sets the mirror mode with a fade-in/fade-out animation. Affects all
  // displays.
  void SetMirrorMode(bool mirror);

  // Sets the display's rotation with animation if available.
  void SetDisplayRotation(int64_t display_id,
                          display::Display::Rotation rotation,
                          display::Display::RotationSource source);

  // Returns the rotation of the display given by |display_id|. This returns
  // the target rotation when the display is being rotated.
  display::Display::Rotation GetTargetRotation(int64_t display_id);

  // Sets the primary display id.
  void SetPrimaryDisplayId(int64_t display_id);

  // WindowTreeHostManager::Observer
  void OnDisplayConfigurationChanged() override;

 protected:
  friend class DisplayConfigurationControllerTestApi;

  // Allow tests to skip animations.
  void ResetAnimatorForTest();

 private:
  class DisplayChangeLimiter;

  // Sets the timeout for the DisplayChangeLimiter if it exists. Call this
  // *before* starting any animations.
  void SetThrottleTimeout(int64_t throttle_ms);
  bool IsLimited();
  void SetDisplayLayoutImpl(std::unique_ptr<display::DisplayLayout> layout);
  void SetMirrorModeImpl(bool mirror);
  void SetPrimaryDisplayIdImpl(int64_t display_id);

  // Returns the ScreenRotationAnimator associated with the |display_id|'s
  // |root_window|. If there is no existing ScreenRotationAnimator for
  // |root_window|, it will make one and store in the |root_window| property
  // |kScreenRotationAnimatorKey|.
  ScreenRotationAnimator* GetScreenRotationAnimatorForDisplay(
      int64_t display_id);

  display::DisplayManager* display_manager_;         // weak ptr
  WindowTreeHostManager* window_tree_host_manager_;  // weak ptr
  std::unique_ptr<DisplayAnimator> display_animator_;
  std::unique_ptr<DisplayChangeLimiter> limiter_;

  base::WeakPtrFactory<DisplayConfigurationController> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(DisplayConfigurationController);
};

}  // namespace ash

#endif  // ASH_DISPLAY_DISPLAY_CONFIGURATION_CONTROLLER_H_
