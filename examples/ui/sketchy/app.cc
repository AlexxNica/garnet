// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/sketchy/app.h"

#include <zircon/syscalls.h>
#include <zircon/types.h>
#include "lib/ui/sketchy/types.h"

namespace {

// Mock a path of wave starting from |start| with |seg_count| segments.
sketchy_lib::StrokePath MockWavePath(glm::vec2 start, uint32_t seg_count) {
  std::vector<sketchy_lib::CubicBezier2> segments;
  segments.reserve(seg_count);
  for (uint32_t i = 0; i < seg_count; i++) {
    segments.push_back({{start},
                        {start + glm::vec2{40, 0}},
                        {start + glm::vec2{40, 40}},
                        {start + glm::vec2{80, 0}}});
    start += glm::vec2{80, 0};
  }
  return sketchy_lib::StrokePath(segments);
}

}  // namespace

namespace sketchy_example {

App::App()
    : loop_(fsl::MessageLoop::GetCurrent()),
      context_(app::ApplicationContext::CreateFromStartupInfo()),
      scene_manager_(
          context_->ConnectToEnvironmentService<scenic::SceneManager>()),
      session_(std::make_unique<scenic_lib::Session>(scene_manager_.get())),
      canvas_(std::make_unique<Canvas>(context_.get())),
      animated_path_at_top_(MockWavePath({570, 350}, 13)),
      animated_path_at_bottom_(MockWavePath({50, 1050}, 26)) {
  session_->set_connection_error_handler([this] {
    FXL_LOG(INFO) << "sketchy_example: lost connection to scenic::Session.";
    loop_->QuitNow();
  });
  scene_manager_.set_connection_error_handler([this] {
    FXL_LOG(INFO)
        << "sketchy_example: lost connection to scenic::SceneManager.";
    loop_->QuitNow();
  });
  scene_manager_->GetDisplayInfo([this](scenic::DisplayInfoPtr display_info) {
    Init(std::move(display_info));
  });
}

void App::Init(scenic::DisplayInfoPtr display_info) {
  scene_ = std::make_unique<Scene>(session_.get(), display_info->physical_width,
                                   display_info->physical_height);

  CubicBezier2 curve1({1180.f, 540.f},
                      {1080.f, 540.f},
                      {1080.f, 640.f},
                      {1080.f, 690.f});
  CubicBezier2 curve2({1080.f, 750.f},
                      {1080.f, 800.f},
                      {1080.f, 900.f},
                      {980.f, 900.f});
  StrokePath path1({curve1, curve2});
  Stroke stroke1(canvas_.get());
  stroke1.SetPath(path1);

  CubicBezier2 curve3({980.f, 720.f},
                      {1040.f, 720.f},
                      {1120.f, 720.f},
                      {1180.f, 720.f});
  StrokePath path2({curve3});
  Stroke stroke2(canvas_.get());
  stroke2.SetPath(path2);

  stable_group_ = std::make_unique<StrokeGroup>(canvas_.get());
  stable_group_->AddStroke(stroke1);
  stable_group_->AddStroke(stroke2);

  animated_stroke_ = std::make_unique<Stroke>(canvas_.get());
  animated_stroke_->SetPath(animated_path_at_top_);
  stable_group_->AddStroke(*animated_stroke_.get());

  scratch_group_ = std::make_unique<StrokeGroup>(canvas_.get());
  fitting_stroke_ = std::make_unique<Stroke>(canvas_.get());

  Stroke tmp_stroke(canvas_.get());
  scratch_group_->AddStroke(tmp_stroke);
  tmp_stroke.Begin({600, 1300});
  tmp_stroke.Extend({{680, 1350}, {720, 1300}, {760, 1350}});
  tmp_stroke.Finish();

  import_node_ = std::make_unique<ImportNode>(
      canvas_.get(), scene_->stroke_group_holder());
  import_node_->AddChild(*stable_group_.get());
  import_node_->AddChild(*scratch_group_.get());

  uint64_t time = zx_time_get(ZX_CLOCK_MONOTONIC);
  canvas_->Present(
      time, std::bind(&App::CanvasCallback, this, std::placeholders::_1));
  session_->Present(time, [](scenic::PresentationInfoPtr info) {});
}

void App::CanvasCallback(scenic::PresentationInfoPtr info) {
  zx_nanosleep(zx_deadline_after(ZX_SEC(1)));
  uint64_t time = zx_time_get(ZX_CLOCK_MONOTONIC);

  // Demo of multi-buffering
  is_animated_stroke_at_top_ = !is_animated_stroke_at_top_;
  if (is_animated_stroke_at_top_) {
    animated_stroke_->SetPath(animated_path_at_top_);
  } else {
    animated_stroke_->SetPath(animated_path_at_bottom_);
  }

  // Demo of stroke fitting
  if (fitting_step_ == 0) {
    scratch_group_->AddStroke(*fitting_stroke_.get());
    fitting_stroke_->Begin({600, 1200});
    fitting_stroke_->Extend({{680, 1250}, {720, 1200}, {760, 1250}});
    fitting_step_ = 1;
  } else if (fitting_step_ == 1) {
    fitting_stroke_->Extend({{800, 1200}, {840, 1250}, {880, 1200}});
    fitting_stroke_->Finish();
    fitting_step_ = 2;
  } else if (fitting_step_ == 2) {
    scratch_group_->RemoveStroke(*fitting_stroke_.get());
    stable_group_->AddStroke(*fitting_stroke_.get());
    fitting_step_ = 3;
  } else if (fitting_step_ == 3) {
    stable_group_->RemoveStroke(*fitting_stroke_.get());
    fitting_step_ = 0;
  }

  canvas_->Present(
      time, std::bind(&App::CanvasCallback, this, std::placeholders::_1));
}

}  // namespace sketchy_example
