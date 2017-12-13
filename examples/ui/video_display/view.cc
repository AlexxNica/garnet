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

  }
};



View::View(app::ApplicationContext* application_context,
           mozart::ViewManagerPtr view_manager,
           fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
    : BaseView(std::move(view_manager),
               std::move(view_owner_request),
               "Shadertoy Example"),
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


// this creates our own vmo, which acts like we are getting information from
// a video source
  zx::vmo::create(num_buffer * kBufferSize, 0, &vmo_);
  // The video source would also specify image size and the number of buffers:
  uint32_t number_of_buffers = 5;
  uint32_t width = 640, height = 480, bpp = 1;
  for (int i = 0; i < 

}

View::~View() = default;

// this creates our own vmo, which acts like we are getting information from
// a video source
void CreateIncomingBuffer(uint64_t num_buffer) {
  // todo(garratt): check response
  zx::vmo::create(num_buffer * kBufferSize, 0, &vmo_);



}




// This loop will puta new image in the buffer to be displayed:
void OnFramePresented(const scenic::PresentationInfoPtr& info){

  auto present_image_callback = [this](scenic::PresentationInfoPtr info) {
      this->OnFramePresented(info);
  };

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
