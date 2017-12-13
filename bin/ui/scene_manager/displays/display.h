// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SCENE_MANAGER_DISPLAYS_DISPLAY_H_
#define GARNET_BIN_UI_SCENE_MANAGER_DISPLAYS_DISPLAY_H_

#include <zircon/types.h>
#include <cstdint>

#include "garnet/bin/ui/scene_manager/displays/display_metrics.h"
#include "lib/fxl/macros.h"

namespace scene_manager {

// Display is a placeholder that provides make-believe values for screen
// resolution, vsync interval, last vsync time, etc.
class Display {
 public:
  Display(DisplayMetrics metrics);

  // Obtain the time of the last Vsync, in nanoseconds.
  zx_time_t GetLastVsyncTime();

  // Obtain the interval between Vsyncs, in nanoseconds.
  zx_time_t GetVsyncInterval() const;

  // Claiming a display means that no other display renderer can use it.
  bool is_claimed() const { return claimed_; }
  void Claim();
  void Unclaim();

  const DisplayMetrics& metrics() const { return metrics_; }

 private:
  // Temporary friendship to allow FrameScheduler to feed back the Vsync timings
  // gleaned from EventTimestamper.  This should go away once we receive real
  // VSync times from the display driver.
  friend class FrameScheduler;
  void set_last_vsync_time(zx_time_t vsync_time);

  zx_time_t last_vsync_time_;
  DisplayMetrics const metrics_;

  bool claimed_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(Display);
};

}  // namespace scene_manager

#endif  // GARNET_BIN_UI_SCENE_MANAGER_DISPLAYS_DISPLAY_H_
