// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_STROKE_STROKE_PATH_H_
#define GARNET_BIN_UI_SKETCHY_STROKE_STROKE_PATH_H_

#include <vector>
#include "garnet/public/lib/escher/geometry/bounding_box.h"
#include "garnet/public/lib/ui/fun/sketchy/fidl/types.fidl.h"
#include "garnet/public/lib/fxl/macros.h"
#include "sketchy/cubic_bezier.h"

namespace sketchy_service {

class StrokePath final {
 public:
  StrokePath() = default;
  explicit StrokePath(sketchy::StrokePathPtr path);

  void ExtendWithCurve(sketchy::CubicBezier2f curve);
  void ExtendWithPath(const StrokePath* path);
  void Reset(size_t segment_count = 0);

  const std::vector<sketchy::CubicBezier2f>& control_points() const {
    return control_points_;
  }
  size_t control_points_size() const {
    return control_points_.size() * sizeof(sketchy::CubicBezier2f);
  }
  const std::vector<sketchy::CubicBezier1f>& re_params() const {
    return re_params_;
  }
  size_t re_params_size() const {
    return re_params_.size() * sizeof(sketchy::CubicBezier1f);
  }
  const std::vector<float>& segment_lengths() const {
    return segment_lengths_;
  }
  float length() const { return length_; }
  bool empty() const { return control_points_.empty(); }
  size_t segment_count() const { return control_points_.size(); }

 private:
  std::vector<sketchy::CubicBezier2f> control_points_;
  std::vector<sketchy::CubicBezier1f> re_params_;
  std::vector<float> segment_lengths_;
  float length_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(StrokePath);
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_STROKE_STROKE_PATH_H_
