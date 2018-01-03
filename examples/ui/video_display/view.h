// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once


// #include "garnet/examples/ui/shadertoy/service/services/shadertoy.fidl.h"
// #include "garnet/examples/ui/shadertoy/service/services/shadertoy_factory.fidl.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/macros.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/view_framework/base_view.h"

namespace video_display {

struct Framebuffer {
    Framebuffer  wait_(fsl::MessageLoop::GetCurrent()->async(),
            buffer->release_fence().get(), ZX_EVENT_SIGNALED) {
    wait_.set_handler(fbl::BindMember(this, &BufferHandler::Handler));
  // Signaled by Renderer when frame is finished, and therefore ready for the
  // ImagePipe consumer to use.
  zx::event acquire_fence;
  // Signaled by the ImagePipe consumer when the framebuffer is no longer used
  // and can therefore be rendered into.
  zx::event release_fence;
  uint32_t image_pipe_id = 0;
  uint32_t buffer_size = 0;
  uint64_t buffer_offset = 0;
  zx::vmo buffer;
  // Sets up the frame buffer with everything it needs to  give access to the
  // buffer
  zx_status_t Create(uint32_t size, uint32_t id, uint64_t offset, zx:vmo main_buffer);

  void AddToImagePipe(scenic::ImagePipePtr &image_pipe, const scenic::ImageInfo &info);

  async::AutoWait wait_;
  bool IsAvailable() { 
};



class BufferHandler {
 public:
  BufferHandler(Buffer *buffer, uint32_t index) :
      buffer_(buffer),
      index_(index),
      wait_(fsl::MessageLoop::GetCurrent()->async(),
            buffer->release_fence().get(), ZX_EVENT_SIGNALED) {
    wait_.set_handler(fbl::BindMember(this, &BufferHandler::Handler));
    auto status = wait_.Begin();
    FXL_DCHECK(status == ZX_OK);
  }

  ~BufferHandler() = default;

  async_wait_result_t Handler(async_t* async, zx_status_t status,
                              const zx_packet_signal* signal) {
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "BufferHandler received an error ("
                       << zx_status_get_string(status) << ").  Exiting.";
        wait_.Cancel();
        fsl::MessageLoop::GetCurrent()->PostQuitTask();
        return ASYNC_WAIT_FINISHED;
      }

      buffer_->Reset();

      auto acq = fidl::Array<zx::event>::New(1);
      auto rel = fidl::Array<zx::event>::New(1);
      buffer_->dupAcquireFence(&acq.front());
      buffer_->dupReleaseFence(&rel.front());

      image_pipe->PresentImage(index_, 0, std::move(acq), std::move(rel),
                               [](scenic::PresentationInfoPtr info) {});

      uint8_t r, g, b;
      hsv_color(hsv_index, &r, &g, &b);
      hsv_index = hsv_inc(hsv_index, 3);
      buffer_->Fill(r, g, b);

      buffer_->Signal();
      return ASYNC_WAIT_AGAIN;
  }

 private:
  Buffer *buffer_;
  uint32_t index_;
  async::AutoWait wait_;
};


class View : public mozart::BaseView {
 public:
  View(app::ApplicationContext* application_context,
       mozart::ViewManagerPtr view_manager,
       fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request);

  ~View() override;

  // mozart::BaseView.
  virtual bool OnInputEvent(mozart::InputEventPtr event) override;

 private:
  // |BaseView|.
  void OnSceneInvalidated(
      scenic::PresentationInfoPtr presentation_info) override;

  app::ApplicationContext* const application_context_;
  fsl::MessageLoop* loop_;

  scenic_lib::ShapeNode node_;
  // std::vector<scenic_lib::ShapeNode> nodes_;

  // Image pipe to send to display
  scenic::ImagePipePtr image_pipe_;

  static constexpr uint32_t kNumFramebuffers = 5;
  Framebuffer frame_buffers_[kNumFramebuffers];
  uint32_t buffer_position = 0;
  zx::vmo vmo_;

  zx_time_t frame_start_time_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(View);
};

}  // namespace shadertoy_client
