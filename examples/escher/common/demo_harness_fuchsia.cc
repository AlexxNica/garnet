// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/escher/common/demo_harness_fuchsia.h"

#include "garnet/examples/escher/common/demo.h"

// When running on Fuchsia, New() instantiates a DemoHarnessFuchsia.
std::unique_ptr<DemoHarness> DemoHarness::New(
    DemoHarness::WindowParams window_params,
    DemoHarness::InstanceParams instance_params) {
  auto harness = new DemoHarnessFuchsia(window_params);
  harness->Init(std::move(instance_params));
  return std::unique_ptr<DemoHarness>(harness);
}

DemoHarnessFuchsia::DemoHarnessFuchsia(WindowParams window_params)
    : DemoHarness(window_params),
      loop_(fsl::MessageLoop::GetCurrent()),
      owned_loop_(loop_ ? nullptr : new fsl::MessageLoop()),
      application_context_(app::ApplicationContext::CreateFromStartupInfo()),
      escher_demo_binding_(this) {
  if (!loop_) {
    loop_ = owned_loop_.get();
  }
}

void DemoHarnessFuchsia::InitWindowSystem() {}

vk::SurfaceKHR DemoHarnessFuchsia::CreateWindowAndSurface(
    const WindowParams& params) {
  VkMagmaSurfaceCreateInfoKHR create_info = {
      .sType = VK_STRUCTURE_TYPE_MAGMA_SURFACE_CREATE_INFO_KHR,
      .pNext = nullptr,
  };
  VkSurfaceKHR surface;
  VkResult err =
      vkCreateMagmaSurfaceKHR(instance(), &create_info, nullptr, &surface);
  FXL_CHECK(!err);
  return surface;
}

void DemoHarnessFuchsia::AppendPlatformSpecificInstanceExtensionNames(
    InstanceParams* params) {
  params->extension_names.insert(VK_KHR_SURFACE_EXTENSION_NAME);
  params->extension_names.insert(VK_KHR_MAGMA_SURFACE_EXTENSION_NAME);
}

void DemoHarnessFuchsia::ShutdownWindowSystem() {}

void DemoHarnessFuchsia::Run(Demo* demo) {
  FXL_CHECK(!demo_);
  demo_ = demo;
  loop_->task_runner()->PostTask([this] { this->RenderFrameOrQuit(); });
  loop_->Run();
}

void DemoHarnessFuchsia::RenderFrameOrQuit() {
  FXL_CHECK(demo_);  // Must be running.
  if (ShouldQuit()) {
    loop_->QuitNow();
    device().waitIdle();
  } else {
    demo_->DrawFrame();
    loop_->task_runner()->PostDelayedTask([this] { this->RenderFrameOrQuit(); },
                                          fxl::TimeDelta::FromMilliseconds(1));
  }
}

void DemoHarnessFuchsia::HandleKeyPress(uint8_t key) {
  demo_->HandleKeyPress(std::string(1, static_cast<char>(key)));
}

void DemoHarnessFuchsia::HandleTouchBegin(uint64_t touch_id,
                                          double xpos,
                                          double ypos) {
  demo_->BeginTouch(touch_id, xpos, ypos);
}

void DemoHarnessFuchsia::HandleTouchContinue(uint64_t touch_id,
                                             double xpos,
                                             double ypos) {
  demo_->ContinueTouch(touch_id, &xpos, &ypos, 1);
}

void DemoHarnessFuchsia::HandleTouchEnd(uint64_t touch_id,
                                        double xpos,
                                        double ypos) {
  demo_->EndTouch(touch_id, xpos, ypos);
}
