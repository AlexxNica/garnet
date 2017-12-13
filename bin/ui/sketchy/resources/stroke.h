// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_RESOURCES_STROKE_H_
#define GARNET_BIN_UI_SKETCHY_RESOURCES_STROKE_H_

#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/scenic/client/session.h"
#include "garnet/bin/ui/sketchy/buffer/mesh_buffer.h"
#include "garnet/bin/ui/sketchy/frame.h"
#include "garnet/bin/ui/sketchy/resources/resource.h"
#include "garnet/bin/ui/sketchy/resources/stroke_path.h"
#include "garnet/bin/ui/sketchy/resources/stroke_tessellator.h"
#include "sketchy/stroke_segment.h"

namespace sketchy_service {

class Stroke;
using StrokePtr = fxl::RefPtr<Stroke>;

class Stroke final : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  explicit Stroke(StrokeTessellator* tessellator);
  bool SetPath(std::unique_ptr<StrokePath> path);

  // Record the command to tessellate and merge the mesh into a larger
  // |mesh_buffer|.
  void TessellateAndMerge(Frame* frame, MeshBuffer* mesh_buffer);

  // Record the command to re-tessellate and merge the mesh into a larger
  // |mesh_buffer|. More specifically, base vertex index will be changed due to
  // re-tessellation.
  void ReTessellateAndMerge(Frame* frame, MeshBuffer* mesh_buffer);

  uint32_t vertex_count() const { return vertex_count_; }
  uint32_t index_count() const { return index_count_; }

 private:
  escher::BufferPtr GetOrCreateUniformBuffer(
      escher::impl::CommandBuffer* command,
      escher::BufferFactory* buffer_factory,
      escher::BufferPtr& buffer, const void* data, size_t size);
  escher::BufferPtr GetOrCreateStorageBuffer(
      escher::impl::CommandBuffer* command,
      escher::BufferFactory* buffer_factory,
      escher::BufferPtr& buffer, const void* data, size_t size);
  // Fore each division, fill its segment index in |division_segment_indices_|.
  // This is a workaround solution to avoid dynamic branching in shader.
  void PrepareDivisionSegmentIndices();

  StrokeTessellator* const tessellator_;

  std::unique_ptr<StrokePath> path_;
  escher::BoundingBox bbox_;
  std::vector<uint32_t> vertex_counts_;
  uint32_t vertex_count_ = 0;
  uint32_t index_count_ = 0;

  uint32_t division_count_ = 0;
  std::vector<uint32_t> division_counts_;
  // Accumulates the previous (self exclusive) division counts.
  std::vector<uint32_t> cumulative_division_counts_;
  // Pre-computes the segment indices for divisions.
  std::vector<uint32_t> division_segment_indices_;

  escher::BufferPtr stroke_info_buffer_;
  escher::BufferPtr control_points_buffer_;
  escher::BufferPtr re_params_buffer_;
  escher::BufferPtr division_counts_buffer_;
  escher::BufferPtr cumulative_division_counts_buffer_;
  escher::BufferPtr division_segment_index_buffer_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Stroke);
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_RESOURCES_STROKE_H_
