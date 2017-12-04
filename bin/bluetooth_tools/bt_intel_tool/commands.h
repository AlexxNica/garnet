// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/bluetooth_tools/lib/command_dispatcher.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

#include "command_channel.h"

namespace bt_intel {

void RegisterCommands(CommandChannel* data,
                      bluetooth::tools::CommandDispatcher* dispatcher);

}  // namespace bt_intel
