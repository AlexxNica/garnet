// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/video_display/view.h"

#if defined(countof)
// TODO(ZX-377): Workaround for compiler error due to Zircon defining countof()
// as a macro.  Redefines countof() using GLM_COUNTOF(), which currently
// provides a more sophisticated implementation anyway.
#undef countof
#include <glm/glm.hpp>
#define countof(X) GLM_COUNTOF(X)
#else
// No workaround required.
#include <glm/glm.hpp>
#endif
#include <glm/gtc/type_ptr.hpp>

#include "garnet/examples/ui/shadertoy/client/glsl_strings.h"
#include "lib/ui/scenic/fidl_helpers.h"

namespace video_display {

namespace {
constexpr uint32_t kShapeWidth = 384;
constexpr uint32_t kShapeHeight = 288;
}  // namespace


struct SupportedImageProperties {
  VkExtent2D size;
  std::vector<VkSurfaceFormatKHR> formats;
};

struct ImagePipeSurface {
  scenic::ImagePipeSyncPtr image_pipe;
  SupportedImageProperties supported_properties;
};

struct PendingImageInfo {
  zx::event release_fence;
  uint32_t image_index;
};


struct Framebuffer {
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
  zx_status_t Create(uint32_t size, uint32_t id, uint64_t offset, zx:vmo main_buffer) {
    buffer_size = size;
    image_pipe_id = id;
    buffer_offset = offset;
    zx_status_t result = main_buffer.duplicate(ZX_RIGHT_SAME_RIGHTS, &buffer);
    if (result != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to duplicate vmo (status: " << result << ").";
      return result;
    }

    result = zx::event::create(0, &acquire_fence);
    if (result != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create event (status: " << result << ").";
      return result;
    }
    result = zx::event::create(0, &acquire_fence);
    if (result != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to create event (status: " << result << ").";
      return result;
    }
    return ZX_OK;
  }

  void AddToImagePipe(scenic::ImagePipePtr &image_pipe, const scenic::ImageInfo &info) {
    
    // Get the vmo handle rights.
    zx_info_handle_basic_t vmo_handle_info;
    zx_status_t result = zx_object_get_info(obj, ZX_INFO_HANDLE_BASIC, &hi, 
                                            sizeof(hi), nullptr, nullptr);
    if (result != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to get handle rights (status: " << result << ").";
      return result;
    }

    zx::vmo render_vmo;
    zx_status_t result = buffer.duplicate(vmo_handle_info.rights & ~ZX_RIGHT_WRITE, &render_vmo);
    if (result != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to duplicate vmo (status: " << result << ").";
      return result;
    }
    image_pipe->AddImage(image_pipe_id, image_info, std::move(render_vmo),
                         scenic::MemoryType::HOST_MEMORY, buffer_offset);
    return ZX_OK;

  }
};


using BufferNotifier  = fbl::Function<void(uint32_t index)>;

class BufferHandler {
 public:
  BufferHandler(Buffer *buffer, uint32_t index, BufferNotifier notifier) :
      buffer_(buffer),
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
  Buffer *buffer_;
  uint32_t index_;
  BufferNotifier notifier_;
  async::AutoWait wait_;
};


void PresentBuffer() {


      auto acq = fidl::Array<zx::event>::New(1);
      auto rel = fidl::Array<zx::event>::New(1);
      buffer_->dupAcquireFence(&acq.front());
      buffer_->dupReleaseFence(&rel.front());

      image_pipe->PresentImage(index_, pres_time, std::move(acq),
                               std::move(rel),
                               fbl::BindMember(this, &BufferHandler::Handler));

      uint8_t r, g, b;
      hsv_color(hsv_index, &r, &g, &b);
      hsv_index = hsv_inc(hsv_index, 3);
      buffer_->Fill(r, g, b);

      buffer_->Signal();
      return ASYNC_WAIT_AGAIN;
}


// Buffer fences and writing states:
// Acq | Rel | State
//  0  |  0  |  
//  0  |  1  |  The buffer is reserved for writing
//  1  |  0  |  The buffer is being read by the renderer
//  1  |  1  |  The buffer is available for whatever


// When a buffer is released, signal that it is available to the writer
// In this case, that means directly write to the buffer then re-present it
void View::BufferReleased(uint32_t  buffer_index) {
  buffers_[buffer_index]->Reset();
  // Call Release() to the camera driver, who then fills it
  // TODO(garratt): for pipelining, should have an idea of who is next...
  //For now fill the buffer here
  
  // Camera driver would then signal:
  IncomingBufferFilled(buffer_index);
}


// We allow the incoming stream to reserve a a write lock on a buffer
// it is writing to.  Reserving this buffer signals that it will be the latest
// buffer to be displayed. In other words, no buffer locked after this buffer
// will be displayed before this buffer.
// If the incoming buffer already filled, the driver could just call 
// IncomingBufferFilled(), which will make sure the buffer is reserved first.
void View::ReserveIncomingBuffer(uint32_t buffer_index) {
  if (buffer_index >= kNumFramebuffers) {
    return; //TODO: error
  }
  // todo: check that no fences are set
  if (frame_buffers_[buffer_index].IsReserved()) {
    return; //todo: Error
  }
  // TODO(garratt): check that we are presenting stuff
  uint64_t pres_time = frame_scheduler_.GetNextPresentationTime();
  
  auto acq = fidl::Array<zx::event>::New(1);
  auto rel = fidl::Array<zx::event>::New(1);
  frame_buffers_[buffer_index]->dupAcquireFence(&acq.front());
  frame_buffers_[buffer_index]->dupReleaseFence(&rel.front());
  // todo: assumes that the image id == buffer_index.
  // I don't see why it shouldn't...
  image_pipe->PresentImage(buffer_index_, pres_time, std::move(acq),
                           std::move(rel),
                           fbl::BindMember(this, &View::OnFramePresented));

  // auto present_image_callback = [this](scenic::PresentationInfoPtr info) {
      // this->OnFramePresented(info);
  // };
}


// When an incomming buffer is  filled, View releases the aquire fence
void View::IncomingBufferFilled(uint32_t buffer_index) {
  if (buffer_index >= kNumFramebuffers) {
    return; //TODO: error
  }
  // If we have not reserved the buffer, do so now:
  // TODO: How do we know that a frame is presented?  eek. assume it is not :(
  //TODO: add ability to check if frame is presented
  ReserveIncomingBuffer(buffer_index);

  // Signal that the buffer is ready to be presented:
  frame_buffers_[buffer_index].Signal();

}

// frame interval:
// After we produce frames, we get a callback with when the frame was produced
// and the presentation interval.  The presentation interval is an upper bound
// on our frame rate, so we mostly just need to make sure that we are
// presenting at our desired rate, and make sure that we don't fall behind the
// presentation times being reported

void View::OnFramePresented(const scenic::PresentationInfoPtr& info) {
  frame_scheduler_.Update(info->presentation_time, info->presentation_interval);
}


// right now, this will just schedule frames as fast as the interval will allow
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
    int64_t diff = presentation_time - requested_presentation_times_.pop_front();
    if (diff > 0) {
      // we are behind - we need to advance our presentation timing
      last_presentation_time_ns_ += diff;
    } // todo error if < 0
    presentation_interval_ns_ = presentation_interval;
  }

};

struct BufferLayout {
  uint64_t vmo_offset;
  uint32_t width, height, bpp;
};


// This would usually be passed to the display app
void CreateIncomingBuffer(std::vector<BufferLayout> *buffers, zx::vmo *buffer_vmo) {
// this creates our own vmo, which acts like we are getting information from
// a video source
  // The video source would also specify image size and the number of buffers:
  uint32_t width = 640, height = 480, bpp = 1;
  uint32_t number_of_buffers = 5;
  uint64_t buffer_width = width * height * bpp; // this is only true for single plane images...
  zx::vmo::create(number_of_buffers * buffer_width, 0, buffer_vmo);
  buffers->clear();
  // these buffers will be contiguous:
  for (uint64_t i = 0; i < number_of_buffers; ++i) {
    BufferLayout layout;
    layout.vmo_offset = i * buffer_width;
    layout.width = width;
    layout.height = height;
    layout.bpp = bpp;
    buffers->push_back(layout);
  }
}

zx_status_t View::SetupBuffers(const std::vector<BufferLayout> &buffer_layouts, 
                   const zx::vmo &vmo) {
  frame_buffers_.resize(buffer_layouts.size());
  frame_handlers_.resize(0);
  auto image_info = scenic::ImageInfo::New();
  image_info->stride = 0;  // inapplicable to GPU_OPTIMAL tiling.
  image_info->tiling = scenic::ImageInfo::Tiling::GPU_OPTIMAL;

  for (uint64_t i = 0; i < buffer_layouts.size(); ++i) {
    Buffer *b = Buffer::NewBuffer(buffer_layouts[i].width,
                                  buffer_layouts[i].height,
                                  vmo,
                                  buffer_layouts[i].vmo_offset);
    if (nullptr == b) {
      return ZX_ERR_INTERNAL;
    }
    frame_buffers_[i] = b;
    image_info->width = buffer_layouts[i].width;
    image_info->height = buffer_layouts[i].height;

    zx::vmo vmo;
    b->dupVmo(&vmo);

    image_pipe->AddImage(image_pipe_id, image_info, std::move(vmo),
                         scenic::MemoryType::HOST_MEMORY,
                         buffer_layouts[i].vmo_offset);
    // now set up a callback for when the release fense is set:
    BufferHandler handler(b, i, fbl::BindMember(this, &View::BufferReleased));
    frame_buffers_.push_back(std::move(handler));
  }
  return ZX_OK;
}

View::View(app::ApplicationContext* application_context,
           mozart::ViewManagerPtr view_manager,
           fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
    : BaseView(std::move(view_manager),
               std::move(view_owner_request),
               "Video Display Example"),
      application_context_(application_context),
      loop_(fsl::MessageLoop::GetCurrent()),
      node_(session()),
      start_time_(zx_time_get(ZX_CLOCK_MONOTONIC)) {


  // Pass the other end of the ImagePipe to the Session, and wrap the
  // resulting resource in a Material.
  uint32_t image_pipe_id = session()->AllocResourceId();
  session()->Enqueue(scenic_lib::NewCreateImagePipeOp(
      image_pipe_id, std::move(image_pipe_.NewRequest())));
  scenic_lib::Material material(session());
  material.SetTexture(image_pipe_id);
  session()->ReleaseResource(image_pipe_id);

  // Create a rounded-rect shape to display the Shadertoy image on.
  scenic_lib::RoundedRectangle shape(session(), kShapeWidth, kShapeHeight, 80,
                                     80, 80, 80);

  node_.SetShape(shape);
  node_.SetMaterial(material);
  parent_node().AddChild(node_);


  // We setup the buffers here, but ideally, this all would happen
  // whenever we got a setup command which gave us buffer information.
  
  std::vector<BufferLayout> buffer_layouts;
  CreateIncomingBuffer(&buffer_layouts, &vmo_);
  SetupBuffers(buffer_layouts, vmo_);


  auto image_info = scenic::ImageInfo::New();
  image_info->width = width();
  image_info->height = height();
  image_info->stride = 0;  // inapplicable to GPU_OPTIMAL tiling.
  image_info->tiling = scenic::ImageInfo::Tiling::GPU_OPTIMAL;

  for (int i = 0; i < kNumFramebuffers; ++i) {
    Framebuffer frame_buffer;
    if (ZX_OK != frame_buffer.Create(buffer_width, i, i*buffer_width, vmo_)) {
      FXL_LOG(ERROR) << "Failed to construct frame.";
      return;
    }
    if (ZX_OK != frame_buffer.AddToImagePipe(image_pipe_, image_info)) {
      FXL_LOG(ERROR) << "Failed to add frame to image pipe.";
      return;
    }
    frame_buffers_.push_back(std::move(frame_buffer));
  }

  //TODO(garratt) stopped here 1/2/18.  
  //  Next up: add the code below into a loop which creates buffers, then adds
  //  this handler, then saves the waits to a vector or whatever.
  sync_t* async = fsl::MessageLoop::GetCurrent()->async();
  async::AutoWait wait(async, buffer->release_fence().get(), ZX_EVENT_SIGNALED);
  wait.set_handler([this, wait, buffer_index](async_t* async, 
                                              zx_status_t status,
                                              const zx_packet_signal* signal) {
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "AutoWait received an error ("
                       << zx_status_get_string(status) << ").  Exiting.";
        wait.Cancel();
        fsl::MessageLoop::GetCurrent()->PostQuitTask();
        return ASYNC_WAIT_FINISHED;
      }
      this->BufferReleased(buffer_index);
      return ASYNC_WAIT_AGAIN;
    });
  auto status = wait_.Begin();
  FXL_DCHECK(status == ZX_OK);
 


}

View::~View() = default;



// This loop will puta new image in the buffer to be displayed:
void OnFramePresented(const scenic::PresentationInfoPtr& info){

  auto present_image_callback = [this](scenic::PresentationInfoPtr info) {
      this->OnFramePresented(info);
  };
  // figure out which frame to display next
  
        // fence.signal(0u, ZX_EVENT_SIGNALED);


  auto acquire_fences = fidl::Array<zx::event>::New(1);
  acquire_fences[0] = std::move(acquire_fence);
  auto release_fences = fidl::Array<zx::event>::New(1);
  release_fences[0] = std::move(release_fence);
  image_pipe_->PresentImage(fb.image_pipe_id, presentation_time,
                            std::move(acquire_fences),
                            std::move(release_fences), present_image_callback);

}



void View::OnSceneInvalidated(scenic::PresentationInfoPtr presentation_info) {
  if (!has_logical_size())
    return;

  // Compute the amount of time that has elapsed since the view was created.
  double seconds_since_frame =
      static_cast<double>(presentation_info->presentation_time - frame_start_time_) /
      1'000'000'000;
  if (seconds_since_frame > .033) {
    // make new frame
    // add it in
    // reset the frame clock
    frame_start_time_ = zx_time_get(ZX_CLOCK_MONOTONIC);
  }

  const float kHalfWidth = logical_size().width * 0.5f;
  const float kHalfHeight = logical_size().height * 0.5f;
  node_.SetTranslation(kHalfWidth, kHalfHeight, 50);
  node_.SetScale(1.0f, 1.0f, 1.0f);

  // The rounded-rectangles are constantly animating; invoke InvalidateScene()
  // to guarantee that OnSceneInvalidated() will be called again.
  InvalidateScene();
}

bool View::OnInputEvent(mozart::InputEventPtr event) {
  return false;
}

}  // namespace shadertoy_client
