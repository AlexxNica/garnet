// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_CLIENT_BUFFER_H_
#define GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_CLIENT_BUFFER_H_

#include <stdint.h>

#include <zx/event.h>
#include <zx/vmo.h>

struct Buffer {
 public:
  ~Buffer();

  void Fill(uint8_t r, uint8_t g, uint8_t b);

  void Reset();
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
     vmo_.duplicate(ZX_RIGHT_SAME_RIGHTS, result);
  }

  static Buffer *NewBuffer(uint32_t width, uint32_t height);

 private:
  Buffer() {};

  zx::vmo vmo_;
  uint32_t *pixels_;
  uint64_t size_;
  uint32_t width_;
  uint32_t height_;

  zx::event acquire_fence_;
  zx::event release_fence_;
};

#endif  // GARNET_LIB_MAGMA_SRC_DISPLAY_PIPE_CLIENT_BUFFER_H_

