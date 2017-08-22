// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/engine.h"

namespace media {

Engine::Engine() {}

Engine::~Engine() {}

void Engine::PrepareInput(Input* input) {
  FTL_DCHECK(input);
  VisitUpstream(input, [](Input* input, Output* output,
                          const StageImpl::UpstreamCallback& callback) {
    FTL_DCHECK(input);
    FTL_DCHECK(output);
    FTL_DCHECK(!input->prepared());
    PayloadAllocator* allocator = input->stage()->PrepareInput(input->index());
    input->set_prepared(true);
    output->stage()->PrepareOutput(output->index(), allocator, callback);
  });
}

void Engine::UnprepareInput(Input* input) {
  FTL_DCHECK(input);
  VisitUpstream(input, [](Input* input, Output* output,
                          const StageImpl::UpstreamCallback& callback) {
    FTL_DCHECK(input);
    FTL_DCHECK(output);
    FTL_DCHECK(input->prepared());
    input->stage()->UnprepareInput(input->index());
    output->stage()->UnprepareOutput(output->index(), callback);
  });
}

void Engine::FlushOutput(Output* output, bool hold_frame) {
  FTL_DCHECK(output);
  if (!output->connected()) {
    return;
  }

  VisitDownstream(
      output, [hold_frame](Output* output, Input* input,
                           const StageImpl::DownstreamCallback& callback) {
        FTL_DCHECK(output);
        FTL_DCHECK(input);
        FTL_DCHECK(input->prepared());
        output->stage()->FlushOutput(output->index());
        input->stage()->FlushInput(input->index(), hold_frame, callback);
      });
}

void Engine::StageNeedsUpdate(StageImpl* stage) {
  FTL_DCHECK(stage);
  if (PushToUpdateBacklog(stage) && update_callback_) {
    update_callback_();
  }
}

bool Engine::UpdateOne() {
  StageImpl* stage = PopFromUpdateBacklog();
  if (!stage) {
    return false;
  }

  stage->UpdateUntilDone();
  return true;
}

void Engine::UpdateUntilDone() {
  {
    ftl::MutexLocker locker(&backlog_mutex_);
    FTL_DCHECK(!suppress_update_callbacks_) << "re-entered UpdateUntilDone.";
    suppress_update_callbacks_ = true;
  }

  while (UpdateOne()) {
    // Do nothing.
  }

  // suppress_update_callbacks_ is set to false when |UpdateOne| returns false.
}

void Engine::VisitUpstream(Input* input, const UpstreamVisitor& visitor) {
  FTL_DCHECK(input);
  ftl::MutexLocker locker(&backlog_mutex_);

  std::queue<Input*> backlog;
  backlog.push(input);

  while (!backlog.empty()) {
    Input* input = backlog.front();
    backlog.pop();
    FTL_DCHECK(input);
    FTL_DCHECK(input->connected());

    Output* output = input->mate();
    StageImpl* output_stage = output->stage();

    visitor(input, output, [output_stage, &backlog](size_t input_index) {
      backlog.push(&output_stage->input(input_index));
    });
  }
}

void Engine::VisitDownstream(Output* output, const DownstreamVisitor& visitor) {
  FTL_DCHECK(output);
  ftl::MutexLocker locker(&backlog_mutex_);

  std::queue<Output*> backlog;
  backlog.push(output);

  while (!backlog.empty()) {
    Output* output = backlog.front();
    backlog.pop();
    FTL_DCHECK(output);
    FTL_DCHECK(output->connected());

    Input* input = output->mate();
    StageImpl* input_stage = input->stage();

    visitor(output, input, [input_stage, &backlog](size_t output_index) {
      backlog.push(&input_stage->output(output_index));
    });
  }
}

bool Engine::PushToUpdateBacklog(StageImpl* stage) {
  FTL_DCHECK(stage);
  ftl::MutexLocker locker(&backlog_mutex_);
  update_backlog_.push(stage);
  return !suppress_update_callbacks_;
}

StageImpl* Engine::PopFromUpdateBacklog() {
  ftl::MutexLocker locker(&backlog_mutex_);

  if (update_backlog_.empty()) {
    suppress_update_callbacks_ = false;
    return nullptr;
  }

  StageImpl* stage = update_backlog_.front();
  update_backlog_.pop();

  return stage;
}

}  // namespace media
