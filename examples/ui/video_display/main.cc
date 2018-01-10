// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-provider/provider.h>

#include "garnet/examples/ui/video_display/view.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/ui/view_framework/view_provider_app.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  fsl::MessageLoop loop;
  FXL_LOG(INFO) << "Set up message loop.";
  trace::TraceProvider trace_provider(loop.async());

  FXL_LOG(INFO) << "Set up trace provider.";
  mozart::ViewProviderApp app([](mozart::ViewContext view_context) {
    return std::make_unique<video_display::View>(
        view_context.application_context, std::move(view_context.view_manager),
        std::move(view_context.view_owner_request));
  });

  loop.Run();
  return 0;
}
