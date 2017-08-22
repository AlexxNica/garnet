// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/refs.h"

#include "apps/media/src/framework/stages/input.h"
#include "apps/media/src/framework/stages/output.h"
#include "apps/media/src/framework/stages/stage_impl.h"
#include "lib/ftl/logging.h"

namespace media {

size_t NodeRef::input_count() const {
  FTL_DCHECK(stage_);
  return stage_->input_count();
}

InputRef NodeRef::input(size_t index) const {
  FTL_DCHECK(stage_);
  FTL_DCHECK(index < stage_->input_count());
  return InputRef(&stage_->input(index));
}

InputRef NodeRef::input() const {
  FTL_DCHECK(stage_);
  FTL_DCHECK(stage_->input_count() == 1);
  return InputRef(&stage_->input(0));
}

size_t NodeRef::output_count() const {
  FTL_DCHECK(stage_);
  return stage_->output_count();
}

OutputRef NodeRef::output(size_t index) const {
  FTL_DCHECK(stage_);
  FTL_DCHECK(index < stage_->output_count());
  return OutputRef(&stage_->output(index));
}

OutputRef NodeRef::output() const {
  FTL_DCHECK(stage_);
  FTL_DCHECK(stage_->output_count() == 1);
  return OutputRef(&stage_->output(0));
}

NodeRef InputRef::node() const {
  return input_ ? NodeRef(input_->stage()) : NodeRef();
}

bool InputRef::connected() const {
  FTL_DCHECK(input_);
  return input_->connected();
}

OutputRef InputRef::mate() const {
  FTL_DCHECK(input_);
  return OutputRef(input_->mate());
}

NodeRef OutputRef::node() const {
  return output_ ? NodeRef(output_->stage()) : NodeRef();
}

bool OutputRef::connected() const {
  FTL_DCHECK(output_);
  return output_->connected();
}

InputRef OutputRef::mate() const {
  FTL_DCHECK(output_);
  return InputRef(output_->mate());
}

}  // namespace media
