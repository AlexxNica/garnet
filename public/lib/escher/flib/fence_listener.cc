// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/flib/fence_listener.h"

#include <zx/time.h>
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/logging.h"

namespace escher {

FenceListener::FenceListener(zx::event fence)
    : fence_(std::move(fence)),
      waiter_(fsl::MessageLoop::GetCurrent()->async(),  // dispatcher
              fence_.get(),                             // handle
              kFenceSignalled)                          // trigger
{
  FXL_DCHECK(fence_);
}

bool FenceListener::WaitReady(fxl::TimeDelta timeout) {
  zx_time_t zx_deadline;
  if (timeout <= fxl::TimeDelta::Zero())
    zx_deadline = 0u;
  else if (timeout == fxl::TimeDelta::Max())
    zx_deadline = ZX_TIME_INFINITE;
  else
    zx_deadline = zx::deadline_after(timeout.ToNanoseconds());

  zx_signals_t pending = 0u;
  while (!ready_) {
    zx_status_t status =
        fence_.wait_one(kFenceSignalled, zx_deadline, &pending);
    FXL_DCHECK(status == ZX_OK || status == ZX_ERR_TIMED_OUT);
    ready_ = pending & kFenceSignalled;
    if (zx_deadline != ZX_TIME_INFINITE)
      break;
  }
  return ready_;
}

void FenceListener::WaitReadyAsync(fxl::Closure ready_callback) {
  if (!ready_callback)
    return;

  // Make sure callback was not set before.
  FXL_DCHECK(!ready_callback_);

  if (ready_) {
    fsl::MessageLoop::GetCurrent()->task_runner()->PostTask(
        std::move(ready_callback));
    return;
  }

  waiter_.set_handler(std::bind(&FenceListener::OnFenceSignalled, this,
                                std::placeholders::_2, std::placeholders::_3));
  zx_status_t status = waiter_.Begin();
  FXL_CHECK(status == ZX_OK);
  ready_callback_ = std::move(ready_callback);
}

async_wait_result_t FenceListener::OnFenceSignalled(
    zx_status_t status,
    const zx_packet_signal* signal) {
  if (status == ZX_OK) {
    zx_signals_t pending = signal->observed;
    FXL_DCHECK(pending & kFenceSignalled);
    FXL_DCHECK(ready_callback_);

    ready_ = true;
    fxl::Closure callback = std::move(ready_callback_);
    waiter_.Cancel();

    callback();
    return ASYNC_WAIT_FINISHED;
  } else {
    FXL_LOG(ERROR) << "FenceListener::OnFenceSignalled received an "
                      "error status code: "
                   << status;

    // TODO(MZ-173): Close the session if there is an error, or if the fence
    // is closed.
    return ASYNC_WAIT_FINISHED;
  }
}

}  // namespace scene_manager
