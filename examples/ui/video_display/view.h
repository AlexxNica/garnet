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
#include "garnet/examples/ui/video_display/buffer.h"
#include <deque>
#include <list>

namespace video_display {

struct BufferLayout {
  uint64_t vmo_offset;
  uint32_t width, height, bpp;
};


class FrameScheduler {
  uint64_t presentation_interval_ns_ = 33000000;
  uint64_t last_presentation_time_ns_ = 0;
  std::deque<uint64_t> requested_presentation_times_;
 public:
  // Get the next frame presentation time
  uint64_t GetNextPresentationTime() {
    last_presentation_time_ns_ += presentation_interval_ns_;
    requested_presentation_times_.push_back(last_presentation_time_ns_);
    return last_presentation_time_ns_;
  }

  void Update(uint64_t presentation_time, uint64_t presentation_interval) {
    if (requested_presentation_times_.size() == 0) {
      return;
    } // todo: add lots of errors
    int64_t diff = presentation_time - requested_presentation_times_.front();
    requested_presentation_times_.pop_front();
    if (diff > 0) {
      // we are behind - we need to advance our presentation timing
      last_presentation_time_ns_ += diff;
    } // todo error if < 0
    presentation_interval_ns_ = presentation_interval;
  }

};


using BufferNotifier  = fbl::Function<void(uint32_t index)>;

class BufferHandler {
 public:
  BufferHandler(Buffer *buffer, uint32_t index, BufferNotifier notifier) :
      // buffer_(buffer),
      index_(index),
      notifier_(fbl::move(notifier)),
      wait_(fsl::MessageLoop::GetCurrent()->async(),
            buffer->release_fence().get(), ZX_EVENT_SIGNALED) {
    wait_.set_handler(fbl::BindMember(this, &BufferHandler::Handler));
    auto status = wait_.Begin();
    FXL_DCHECK(status == ZX_OK);
  }

  ~BufferHandler() = default;
  
  // This function is called when the release fence is signalled
  async_wait_result_t Handler(async_t* async, zx_status_t status,
                              const zx_packet_signal* signal) {
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "BufferHandler received an error ("
                       << zx_status_get_string(status) << ").  Exiting.";
        wait_.Cancel();
        return ASYNC_WAIT_FINISHED;
      }
      notifier_(index_);
      return ASYNC_WAIT_AGAIN;
  }

 private:
  // Buffer *buffer_;
  uint32_t index_;
  BufferNotifier notifier_;
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

// When a buffer is released, signal that it is available to the writer
// In this case, that means directly write to the buffer then re-present it
void BufferReleased(uint32_t  buffer_index);

void OnFramePresented(const scenic::PresentationInfoPtr& info);

// When an incoming buffer is filled, View releases the aquire fence
void IncomingBufferFilled(uint32_t buffer_index);

// called to reserve a buffer for writing
void ReserveIncomingBuffer(uint32_t buffer_index);

zx_status_t SetupBuffers(const std::vector<BufferLayout> &buffer_layouts, 
                   const zx::vmo &vmo);
 private:
  // |BaseView|.
  // void OnSceneInvalidated(
      // scenic::PresentationInfoPtr presentation_info) override;

  // app::ApplicationContext* const application_context_;
  fsl::MessageLoop* loop_;

  scenic_lib::ShapeNode node_;

  // Image pipe to send to display
  scenic::ImagePipePtr image_pipe_;

  std::vector<Buffer*> frame_buffers_;
  std::list<BufferHandler> frame_handlers_; // todo: this can't be resized...
  // uint32_t buffer_position = 0;
  zx::vmo vmo_;
  FrameScheduler frame_scheduler_;
  // zx_time_t frame_start_time_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(View);
};

}  // namespace video_display
