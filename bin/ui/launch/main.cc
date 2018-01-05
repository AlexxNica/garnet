// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zx/channel.h>

#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/svc/cpp/services.h"
#include "lib/ui/presentation/fidl/presenter.fidl.h"
#include "lib/ui/views/fidl/view_provider.fidl.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  const auto& positional_args = command_line.positional_args();
  if (positional_args.empty()) {
    FXL_LOG(ERROR) << "Launch requires the url of a view provider application "
                      "to launch.";
    return 1;
  }

  fsl::MessageLoop loop;
  auto application_context_ = app::ApplicationContext::CreateFromStartupInfo();

  // Launch application.
  app::Services services;
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = positional_args[0];
  for (size_t i = 1; i < positional_args.size(); ++i)
    launch_info->arguments.push_back(positional_args[i]);
  launch_info->service_request = services.NewRequest();
  app::ApplicationControllerPtr controller;
  application_context_->launcher()->CreateApplication(std::move(launch_info),
                                                      controller.NewRequest());
  controller.set_connection_error_handler([&loop] {
    FXL_LOG(INFO) << "Launched application terminated.";
    loop.PostQuitTask();
  });

  // Create the view.
  fidl::InterfacePtr<mozart::ViewProvider> view_provider;
  services.ConnectToService(view_provider.NewRequest());
  fidl::InterfaceHandle<mozart::ViewOwner> view_owner;
  view_provider->CreateView(view_owner.NewRequest(), nullptr);

  // Ask the presenter to display it.
  auto presenter =
      application_context_->ConnectToEnvironmentService<mozart::Presenter>();
  presenter->Present(std::move(view_owner), nullptr);

  // Done!
  loop.Run();
  return 0;
}
