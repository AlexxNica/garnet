// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <set>
#include "garnet/bin/ui/sketchy/buffer.h"
#include "garnet/bin/ui/sketchy/resources/mesh_buffer.h"
#include "garnet/bin/ui/sketchy/resources/resource.h"
#include "garnet/bin/ui/sketchy/resources/stroke.h"
#include "lib/escher/geometry/bounding_box.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/scenic/client/session.h"

namespace sketchy_service {

class StrokeGroup;
using StrokeGroupPtr = fxl::RefPtr<StrokeGroup>;

class StrokeGroup final : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  StrokeGroup(scenic_lib::Session* session,
              escher::BufferFactory* buffer_factory);

  // Record the stroke to add.
  bool AddStroke(StrokePtr stroke);
  // TODO(MZ-269): Implement.
  // bool RemoveStroke(StrokePtr stroke);

  void SetNeedsReTessellation() { needs_re_tessellation_ = true; }

  // Record the command to update the mesh.
  void UpdateMesh(escher::impl::CommandBuffer* command,
                  escher::BufferFactory* buffer_factory);

  const scenic_lib::ShapeNode& shape_node() const { return shape_node_; }

 private:
  // Record the command to merge the strokes to add.
  void MergeStrokes(escher::impl::CommandBuffer* command,
                    escher::BufferFactory* buffer_factory);

  // Record the command to re-tessellate the strokes.
  void ReTessellateStrokes(escher::impl::CommandBuffer* command,
                           escher::BufferFactory* buffer_factory);

  scenic_lib::ShapeNode shape_node_;
  scenic_lib::Mesh mesh_;
  scenic_lib::Material material_;

  std::set<StrokePtr> strokes_to_add_;
  std::set<StrokePtr> strokes_;
  MeshBuffer mesh_buffer_;
  bool needs_re_tessellation_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(StrokeGroup);
};

}  // namespace sketchy_service
