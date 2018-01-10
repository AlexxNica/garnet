// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_CLIENT_BUFFER_H_
#define GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_CLIENT_BUFFER_H_

#include <stdint.h>

#include <zx/event.h>
#include <zx/vmo.h>
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "zircon/status.h"

struct Buffer {
 public:
  ~Buffer();

  void Fill(uint8_t r, uint8_t g, uint8_t b);

  void Reset();
  void Reserve();
  void Signal();
  void Release();

  const zx::event& acqure_fence() { return acquire_fence_; }
  const zx::event& release_fence() { return release_fence_; }

  void dupAcquireFence(zx::event *result) {
     acquire_fence_.duplicate(ZX_RIGHT_SAME_RIGHTS, result);
  }
  
  void dupReleaseFence(zx::event *result) {
     release_fence_.duplicate(ZX_RIGHT_SAME_RIGHTS, result);
  }

  void dupVmo(zx::vmo *result) {
     vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS & ~ZX_RIGHT_WRITE, result);
  }

  static Buffer *NewBuffer(uint32_t width, uint32_t height, const zx::vmo &main_buffer, uint64_t offset);

  bool IsReleased() { 
    // TODO: make sure this actually works, and doesn't just always throw the
    // timeout...
    zx_signals_t observed;
    zx_status_t ret = acquire_fence_.wait_one(ZX_EVENT_SIGNALED, 0, 
                                                       &observed);
    if (ret == ZX_OK) {
        FXL_LOG(INFO) << "Wait returned OK";
        return true;
    }
    if (ZX_EVENT_SIGNALED & observed) {
        FXL_LOG(INFO) << "Wait timed out, ZX_EVENT_SIGNALED is set.";
        return true;
    }
    FXL_LOG(INFO) << "Wait timed out, ZX_EVENT_SIGNALED is cleared.";
    return false;

  }
  bool IsReserved() { 
    // TODO: make sure this actually works, and doesn't just always throw the
    // timeout...
    zx_signals_t observed;
    zx_status_t ret = acquire_fence_.wait_one(ZX_EVENT_SIGNALED, 0, 
                                                       &observed);
    if (ret == ZX_OK) {
        FXL_LOG(INFO) << "Wait returned OK";
        return false;
    }
    if (ZX_EVENT_SIGNALED & observed) {
        FXL_LOG(INFO) << "Wait timed out, ZX_EVENT_SIGNALED is set.";
        return false;
    }
    FXL_LOG(INFO) << "Wait timed out, ZX_EVENT_SIGNALED is cleared.";
    return true;

  }

  uint32_t *pixels_; //DEBUG
 private:
  Buffer() {};

  zx::vmo vmo_;
  uint64_t vmo_offset;
  uint64_t size_;
  uint32_t width_;
  uint32_t height_;

  zx::event acquire_fence_;
  zx::event release_fence_;
};

#endif  // GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_CLIENT_BUFFER_H_

