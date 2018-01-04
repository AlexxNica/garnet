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

  bool IsReserved() { 
    // TODO: make sure this actually works, and doesn't just always throw the
    // timeout...
    return ZX_ERR_TIMED_OUT == acquire_fence_.wait_one(ZX_EVENT_SIGNALED, 0, 
                                                       NULL);
  }

 private:
  Buffer() {};

  zx::vmo vmo_;
  uint64_t vmo_offset;
  uint32_t *pixels_;
  uint64_t size_;
  uint32_t width_;
  uint32_t height_;

  zx::event acquire_fence_;
  zx::event release_fence_;
};

#endif  // GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_CLIENT_BUFFER_H_

