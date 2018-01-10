
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

#include "lib/ui/scenic/fidl_helpers.h"

namespace video_display {

namespace {
constexpr uint32_t kShapeWidth = 384;
constexpr uint32_t kShapeHeight = 288;
}  // namespace



// Buffer fences and writing states:
// Acq | Rel | State
//  0  |  X  |  The buffer is reserved for writing
//  1  |  0  |  The buffer is being read by the renderer
//  1  |  1  |  The buffer is available for whatever


// When a buffer is released, signal that it is available to the writer
// In this case, that means directly write to the buffer then re-present it
void View::BufferReleased(uint32_t  buffer_index) {
  FXL_LOG(INFO) << "BufferReleased " << buffer_index;
  frame_buffers_[buffer_index]->IsReleased();
  frame_buffers_[buffer_index]->Reset();
  frame_locks_[buffer_index] = false;

  // Call Release() to the camera driver, who then fills it
  // TODO(garratt): for pipelining, should have an idea of who is next...
  //For now fill the buffer here
    FXL_LOG(INFO) << "Writing to Buffer " << buffer_index;
  fake_video_source_.WriteToBuffer(frame_buffers_[buffer_index]);
  // InvalidateScene();
    FXL_LOG(INFO) << "signalling filled " << buffer_index;
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
    FXL_LOG(INFO) << "Reserving incoming Buffer " << buffer_index;
  if (buffer_index >= frame_buffers_.size()) {
    FXL_LOG(ERROR) << "Attempting to Reserve incoming Buffer " << buffer_index << " > max ";
    return; //TODO: error
  }
  // todo: check that no fences are set
  if (frame_locks_[buffer_index]) {
    FXL_LOG(ERROR) << "Attempting to Reserve already reserved Buffer " << buffer_index;
    return; //todo: Error
  }
  frame_locks_[buffer_index] = true;
  // TODO(garratt): check that we are presenting stuff
  uint64_t pres_time = frame_scheduler_.GetNextPresentationTime(buffer_index);
  
  auto acq = fidl::Array<zx::event>::New(1);
  auto rel = fidl::Array<zx::event>::New(1);
  frame_buffers_[buffer_index]->dupAcquireFence(&acq.front());
  frame_buffers_[buffer_index]->dupReleaseFence(&rel.front());
  // todo: assumes that the image id == buffer_index.
  // I don't see why it shouldn't...
  FXL_LOG(INFO) << "presenting Buffer " << buffer_index << " at " << pres_time;
  image_pipe_->PresentImage(buffer_index + 1, pres_time, std::move(acq),
                           std::move(rel),
                           fbl::BindMember(this, &View::OnFramePresented));

}


// When an incomming buffer is  filled, View releases the aquire fence
void View::IncomingBufferFilled(uint32_t buffer_index) {
    FXL_LOG(INFO) << "View::IncomingBufferFilled " << buffer_index;
  if (buffer_index >= frame_buffers_.size()) {
    return; //TODO: error
  }
  // If we have not reserved the buffer, do so now. ReserveIncomingBuffer
  // will quietly return if the buffer is already reserved.
  ReserveIncomingBuffer(buffer_index);

  // DEBUG:
  FXL_LOG(INFO) << "----  Buffer value: " << frame_buffers_[buffer_index]->pixels_[200];
  // Signal that the buffer is ready to be presented:
  frame_buffers_[buffer_index]->Signal();

}

// frame interval:
// After we produce frames, we get a callback with when the frame was produced
// and the presentation interval.  The presentation interval is an upper bound
// on our frame rate, so we mostly just need to make sure that we are
// presenting at our desired rate, and make sure that we don't fall behind the
// presentation times being reported

void View::OnFramePresented(const scenic::PresentationInfoPtr& info) {
  FXL_LOG(INFO) << "Got View::OnFramePresented for " << info->presentation_time;
  uint32_t frame = frame_scheduler_.Update(info->presentation_time, info->presentation_interval);
  if(!frame_buffers_[frame]->IsReleased()) {
      FXL_LOG(INFO) << "Release Fence is not signalled!";
  }
  frame_buffers_[frame]->Release();
  frame_buffers_[frame]->IsReleased();
}




// This would usually be passed to the display app
void CreateIncomingBuffer(std::vector<BufferLayout> *buffers, zx::vmo *buffer_vmo) {
// this creates our own vmo, which acts like we are getting information from
// a video source
  // The video source would also specify image size and the number of buffers:
  uint32_t width = 640, height = 480, bpp = 4;
  uint32_t number_of_buffers = 1;
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
  // frame_handlers_.resize(0);

  for (uint64_t i = 0; i < buffer_layouts.size(); ++i) {
    FXL_LOG(INFO) << "Creating buffer " << i;
    Buffer *b = Buffer::NewBuffer(buffer_layouts[i].width,
                                  buffer_layouts[i].height,
                                  vmo,
                                  buffer_layouts[i].vmo_offset);
    if (nullptr == b) {
      return ZX_ERR_INTERNAL;
    }
    frame_buffers_[i] = b;
    FXL_LOG(INFO) << "Creating ImageInfo " << i;
  auto image_info = scenic::ImageInfo::New();
  image_info->stride = buffer_layouts[i].width * buffer_layouts[i].bpp;  // inapplicable to GPU_OPTIMAL tiling.
  image_info->tiling = scenic::ImageInfo::Tiling::LINEAR;
    image_info->width = buffer_layouts[i].width;
    image_info->height = buffer_layouts[i].height;

    zx::vmo vmo;
    FXL_LOG(INFO) << "Duping VMO " << i;
    b->dupVmo(&vmo);

    FXL_LOG(INFO) << "adding Image " << i;
    image_pipe_->AddImage(i+1, std::move(image_info), std::move(vmo),
                         scenic::MemoryType::HOST_MEMORY,
                         buffer_layouts[i].vmo_offset);
    // now set up a callback for when the release fense is set:
    // BufferHandler handler(b, i, fbl::BindMember(this, &View::BufferReleased));
    // frame_handlers_.push_back(std::move(handler));
    FXL_LOG(INFO) << "adding handler " << i;
    frame_handlers_.emplace_back(b, i, fbl::BindMember(this, &View::BufferReleased));
    frame_locks_.push_back(false);
  }
  return ZX_OK;
}

View::View(app::ApplicationContext* application_context,
           mozart::ViewManagerPtr view_manager,
           fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
    : BaseView(std::move(view_manager),
               std::move(view_owner_request),
               "Video Display Example"),
      // application_context_(application_context),
      loop_(fsl::MessageLoop::GetCurrent()),
      node_(session()) {
      // start_time_(zx_time_get(ZX_CLOCK_MONOTONIC)) {

FXL_LOG(INFO) << "Creating View";
  // Pass the other end of the ImagePipe to the Session, and wrap the
  // resulting resource in a Material.
  uint32_t image_pipe_id = session()->AllocResourceId();
  session()->Enqueue(scenic_lib::NewCreateImagePipeOp(
      image_pipe_id, image_pipe_.NewRequest()));
  scenic_lib::Material material(session());
  material.SetTexture(image_pipe_id);
  session()->ReleaseResource(image_pipe_id);

  // Create a rounded-rect shape to display the Shadertoy image on.
  scenic_lib::RoundedRectangle shape(session(), kShapeWidth, kShapeHeight, 80,
                                     80, 80, 80);

  // const float kHalfWidth = logical_size().width * 0.5f;
  // const float kHalfHeight = logical_size().height * 0.5f;
  node_.SetShape(shape);
  node_.SetMaterial(material);
  parent_node().AddChild(node_);
    node_.SetTranslation(640, 480, 50);
  InvalidateScene();

FXL_LOG(INFO) << "Creating View - set up image pipe";

  // We setup the buffers here, but ideally, this all would happen
  // whenever we got a setup command which gave us buffer information.
  
  std::vector<BufferLayout> buffer_layouts;
  CreateIncomingBuffer(&buffer_layouts, &vmo_);
FXL_LOG(INFO) << "Creating View - created vmo";
  SetupBuffers(buffer_layouts, vmo_);
FXL_LOG(INFO) << "Creating View - set up buffers";
  
  // Fill a couple of buffers to get things started.
  // Can just call BufferReleased for now, but ideally, this would all be done
  // by the video driver.
  // BufferReleased(0);
  // BufferReleased(1);
  // BufferReleased(2);
FXL_LOG(INFO) << "Creating View - loaded buffers";



}

View::~View() = default;


void View::OnSceneInvalidated(scenic::PresentationInfoPtr presentation_info) {
FXL_LOG(INFO) << "View::OnSceneInvalidated";
  if (!has_logical_size())
    return;

  // Compute the amount of time that has elapsed since the view was created.
  double seconds =
      static_cast<double>(presentation_info->presentation_time) /
      1'000'000'000;

  const float kHalfWidth = logical_size().width * 0.5f;
  const float kHalfHeight = logical_size().height * 0.5f;

    // Compute the translation for kSwirling mode.
    // Each node has a slightly different speed.
    node_.SetTranslation(kHalfWidth * (1.1 + sin(seconds * 0.8)),
        kHalfHeight * (1.2 + sin(seconds * 0.6)),  50.0);
   FXL_LOG(INFO) << "Setting translation to " 
       << kHalfWidth * (1.1 + sin(seconds * 0.8)) << ", "
       << kHalfHeight * (1.2 + sin(seconds * 0.6));

  // The rounded-rectangles are constantly animating; invoke InvalidateScene()
  // to guarantee that OnSceneInvalidated() will be called again.
  InvalidateScene();
}

bool View::OnInputEvent(mozart::InputEventPtr event) {
  return false;
}

}  // namespace shadertoy_client
