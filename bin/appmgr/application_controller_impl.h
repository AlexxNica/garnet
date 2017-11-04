// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_APPLICATION_CONTROLLER_IMPL_H_
#define GARNET_BIN_APPMGR_APPLICATION_CONTROLLER_IMPL_H_

#include <fs/pseudo-dir.h>
#include <zx/process.h>

#include "garnet/bin/appmgr/application_namespace.h"
#include "garnet/lib/farfs/file_system.h"
#include "lib/app/fidl/application_controller.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fsl/tasks/message_loop_handler.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace app {
class JobHolder;

class ApplicationControllerImpl : public ApplicationController,
                                  public fsl::MessageLoopHandler {
 public:
  ApplicationControllerImpl(
      fidl::InterfaceRequest<ApplicationController> request,
      JobHolder* job_holder,
      std::unique_ptr<archive::FileSystem> fs,
      zx::process process,
      std::string url,
      std::string label,
      fxl::RefPtr<ApplicationNamespace> application_namespace,
      zx::channel service_dir_channel);
  ~ApplicationControllerImpl() override;

  const std::string& label() const { return label_; }
  const fbl::RefPtr<fs::PseudoDir>& info_dir() const { return info_dir_; }

  // |ApplicationController| implementation:
  void Kill() override;
  void Detach() override;
  void Wait(const WaitCallback& callback) override;

 private:
  // |fsl::MessageLoopHandler| implementation:
  void OnHandleReady(zx_handle_t handle,
                     zx_signals_t pending,
                     uint64_t count) override;

  bool SendReturnCodeIfTerminated();

  fidl::Binding<ApplicationController> binding_;
  JobHolder* job_holder_;
  std::unique_ptr<archive::FileSystem> fs_;
  zx::process process_;
  std::string label_;
  std::vector<WaitCallback> wait_callbacks_;
  fbl::RefPtr<fs::PseudoDir> info_dir_;

  fxl::RefPtr<ApplicationNamespace> application_namespace_;

  fsl::MessageLoop::HandlerKey termination_handler_ = 0u;

  FXL_DISALLOW_COPY_AND_ASSIGN(ApplicationControllerImpl);
};

}  // namespace app

#endif  // GARNET_BIN_APPMGR_APPLICATION_CONTROLLER_IMPL_H_
