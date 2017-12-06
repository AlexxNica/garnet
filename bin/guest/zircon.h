// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_ZIRCON_H_
#define GARNET_BIN_GUEST_ZIRCON_H_

#include <zircon/types.h>

zx_status_t setup_zircon(const uintptr_t addr,
                         const size_t vmo_size,
                         const uintptr_t first_page,
                         const uintptr_t acpi_off,
                         const int fd,
                         const char* bootdata_path,
                         const char* cmdline,
                         uintptr_t* guest_ip,
                         uintptr_t* ramdisk_off);

#endif  // GARNET_BIN_GUEST_ZIRCON_H_
