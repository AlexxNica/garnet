// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef GARNET_LIB_MACHINA_GPU_BITMAP_H_
#define GARNET_LIB_MACHINA_GPU_BITMAP_H_

#include <stdint.h>

#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

namespace machina {

struct GpuRect {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;
};

// A contiguous 2D display buffer.
class GpuBitmap {
 public:
  // Create an empty bitmap.
  GpuBitmap();

  // Create a bitmap with an existing buffer.
  GpuBitmap(uint32_t width, uint32_t height, uint8_t* buffer);

  // Create a bitmap for a given size.
  GpuBitmap(uint32_t width, uint32_t height);

  // Move semantics.
  GpuBitmap(GpuBitmap&&);
  GpuBitmap& operator=(GpuBitmap&& o);

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(GpuBitmap);

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  uint8_t* buffer() const { return ptr_; }

  // Draws a portion of another bitmap into this one.
  //
  // |source_rect| and |dest_rect| must both be wholely contained within
  // the respective bitmaps and must have the same width and height.
  void DrawBitmap(const GpuBitmap& from,
                  const GpuRect& source_rect,
                  const GpuRect& dest_rect);

 private:
  uint32_t width_;
  uint32_t height_;

  // Reading of the buffer should always occur through |ptr_| as |buffer_| is
  // not used when operating when an externally-managed buffer.
  fbl::unique_ptr<uint8_t[]> buffer_;
  uint8_t* ptr_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_GPU_BITMAP_H_
