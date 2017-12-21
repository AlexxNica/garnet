// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/sketchy/canvas.h"

namespace sketchy_lib {

Resource::Resource(Canvas* canvas)
    : canvas_(canvas), id_(canvas_->AllocateResourceId()) {}

Resource::~Resource() {
  auto release_resource = sketchy::ReleaseResourceOp::New();
  release_resource->id = id_;
  auto op = sketchy::Op::New();
  op->set_release_resource(std::move(release_resource));
  canvas_->ops_.push_back(std::move(op));
}

void Resource::EnqueueOp(sketchy::OpPtr op) {
  canvas_->ops_.push_back(std::move(op));
}

void Resource::EnqueueCreateResourceOp(ResourceId resource_id,
                                       sketchy::ResourceArgsPtr args) {
  auto create_resource = sketchy::CreateResourceOp::New();
  create_resource->id = resource_id;
  create_resource->args = std::move(args);
  auto op = sketchy::Op::New();
  op->set_create_resource(std::move(create_resource));
  EnqueueOp(std::move(op));
}

void Resource::EnqueueImportResourceOp(ResourceId resource_id,
                                       zx::eventpair token,
                                       scenic::ImportSpec spec) {
  auto import_resource = scenic::ImportResourceOp::New();
  import_resource->id = resource_id;
  import_resource->token = std::move(token);
  import_resource->spec = spec;
  auto op = sketchy::Op::New();
  op->set_scenic_import_resource(std::move(import_resource));
  EnqueueOp(std::move(op));
}

Stroke::Stroke(Canvas* canvas) : Resource(canvas) {
  sketchy::StrokePtr stroke = sketchy::Stroke::New();
  auto resource_args = sketchy::ResourceArgs::New();
  resource_args->set_stroke(std::move(stroke));
  EnqueueCreateResourceOp(id(), std::move(resource_args));
}

void Stroke::SetPath(StrokePath& path) {
  auto set_stroke_path = sketchy::SetStrokePathOp::New();
  set_stroke_path->stroke_id = id();
  set_stroke_path->path = path.NewSketchyStrokePath();
  auto op = sketchy::Op::New();
  op->set_set_path(std::move(set_stroke_path));
  EnqueueOp(std::move(op));
}

void Stroke::Begin(glm::vec2 pt) {
  auto begin_stroke = sketchy::BeginStrokeOp::New();
  begin_stroke->stroke_id = id();
  auto touch = sketchy::Touch::New();
  touch->position = scenic::vec2::New();
  touch->position->x = pt.x;
  touch->position->y = pt.y;
  begin_stroke->touch = std::move(touch);
  auto op = sketchy::Op::New();
  op->set_begin_stroke(std::move(begin_stroke));
  EnqueueOp(std::move(op));
}

void Stroke::Extend(std::vector<glm::vec2> pts) {
  auto extend_stroke = sketchy::ExtendStrokeOp::New();
  extend_stroke->stroke_id = id();
  auto touches = ::fidl::Array<sketchy::TouchPtr>::New(pts.size());
  for (size_t i = 0; i < pts.size(); i++) {
    touches[i] = sketchy::Touch::New();
    touches[i]->position = scenic::vec2::New();
    touches[i]->position->x = pts[i].x;
    touches[i]->position->y = pts[i].y;
  }
  extend_stroke->touches = std::move(touches);
  // TODO(MZ-269): Populate predicted touches.
  extend_stroke->predicted_touches = ::fidl::Array<sketchy::TouchPtr>::New(0);
  auto op = sketchy::Op::New();
  op->set_extend_stroke(std::move(extend_stroke));
  EnqueueOp(std::move(op));
}

void Stroke::Finish() {
  auto finish_stroke = sketchy::FinishStrokeOp::New();
  finish_stroke->stroke_id = id();
  auto op = sketchy::Op::New();
  op->set_finish_stroke(std::move(finish_stroke));
  EnqueueOp(std::move(op));
}

StrokeGroup::StrokeGroup(Canvas* canvas) : Resource(canvas) {
  sketchy::StrokeGroupPtr stroke_group = sketchy::StrokeGroup::New();
  auto resource_args = sketchy::ResourceArgs::New();
  resource_args->set_stroke_group(std::move(stroke_group));
  EnqueueCreateResourceOp(id(), std::move(resource_args));
}

void StrokeGroup::AddStroke(Stroke& stroke) {
  auto add_stroke = sketchy::AddStrokeOp::New();
  add_stroke->stroke_id = stroke.id();
  add_stroke->group_id = id();
  auto op = sketchy::Op::New();
  op->set_add_stroke(std::move(add_stroke));
  EnqueueOp(std::move(op));
}

ImportNode::ImportNode(Canvas* canvas, scenic_lib::EntityNode& export_node)
    : Resource(canvas) {
  zx::eventpair token;
  export_node.ExportAsRequest(&token);
  EnqueueImportResourceOp(id(), std::move(token), scenic::ImportSpec::NODE);
}

void ImportNode::AddChild(const Resource& child) {
  auto add_child = scenic::AddChildOp::New();
  add_child->child_id = child.id();
  add_child->node_id = id();
  auto op = sketchy::Op::New();
  op->set_scenic_add_child(std::move(add_child));
  EnqueueOp(std::move(op));
}

}  // namespace sketchy_lib
