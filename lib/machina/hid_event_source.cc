// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/hid_event_source.h"

#include <fcntl.h>
#include <threads.h>
#include <unistd.h>

#include <fbl/auto_lock.h>
#include <fbl/unique_fd.h>
#include <fdio/watcher.h>
#include <hid/hid.h>
#include <zircon/compiler.h>
#include <zircon/device/input.h>
#include <zircon/types.h>

namespace machina {

static const char* kInputDirPath = "/dev/class/input";

zx_status_t HidInputDevice::Start() {
  thrd_t thread;
  auto hid_event_thread = [](void* arg) {
    return reinterpret_cast<HidInputDevice*>(arg)->HidEventLoop();
  };
  int ret = thrd_create(&thread, hid_event_thread, this);
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  ret = thrd_detach(thread);
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t HidInputDevice::HandleHidKeys(const hid_keys_t& curr_keys) {
  bool send_barrier = false;

  // Send key-down events.
  uint8_t keycode;
  hid_keys_t pressed;
  hid_kbd_pressed_keys(&prev_keys_, &curr_keys, &pressed);
  hid_for_every_key(&pressed, keycode) {
    SendKeyEvent(keycode, true);
    send_barrier = true;
  }

  // Send key-up events.
  hid_keys_t released;
  hid_kbd_released_keys(&prev_keys_, &curr_keys, &released);
  hid_for_every_key(&released, keycode) {
    SendKeyEvent(keycode, false);
    send_barrier = true;
  }

  if (send_barrier) {
    SendBarrier();
  }

  prev_keys_ = curr_keys;
  return ZX_OK;
}

zx_status_t HidInputDevice::HidEventLoop() {
  uint8_t report[8];
  while (true) {
    ssize_t r = read(fd_.get(), report, sizeof(report));
    if (r != sizeof(report)) {
      fprintf(stderr, "failed to read from input device\n");
      return ZX_ERR_IO;
    }

    hid_keys_t curr_keys;
    hid_kbd_parse_report(report, &curr_keys);

    zx_status_t status = HandleHidKeys(curr_keys);
    if (status != ZX_OK) {
      fprintf(stderr, "Failed to handle HID keys.\n");
      return status;
    }
  }
  return ZX_OK;
}

void HidInputDevice::SendKeyEvent(uint32_t hid_usage, bool pressed) {
  InputEvent event;
  event.type = InputEventType::KEYBOARD;
  event.key = {
      .hid_usage = hid_usage,
      .state = pressed ? KeyState::PRESSED : KeyState::RELEASED,
  };
  input_dispatcher_->PostEvent(event);
}

void HidInputDevice::SendBarrier() {
  InputEvent event;
  event.type = InputEventType::BARRIER;
  input_dispatcher_->PostEvent(event);
}

zx_status_t HidEventSource::Start() {
  thrd_t thread;
  int ret = thrd_create(&thread, &HidEventSource::WatchInputDirectory, this);
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  ret = thrd_detach(thread);
  if (ret != thrd_success) {
    return ZX_ERR_INTERNAL;
  }
  return ZX_OK;
}

zx_status_t HidEventSource::WatchInputDirectory(void* arg) {
  fbl::unique_fd dirfd;
  {
    int fd = open(kInputDirPath, O_DIRECTORY | O_RDONLY);
    if (fd < 0) {
      return ZX_ERR_IO;
    }
    dirfd.reset(fd);
  }

  auto callback = [](int dirfd, int event, const char* fn, void* cookie) {
    return reinterpret_cast<HidEventSource*>(cookie)->AddInputDevice(dirfd,
                                                                     event, fn);
  };
  zx_status_t status =
      fdio_watch_directory(dirfd.get(), callback, ZX_TIME_INFINITE, arg);

  return status;
}

zx_status_t HidEventSource::AddInputDevice(int dirfd,
                                           int event,
                                           const char* fn) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  }

  fbl::unique_fd fd;
  int raw_fd = openat(dirfd, fn, O_RDONLY);
  if (raw_fd < 0) {
    fprintf(stderr, "Failed to open device %s/%s\n", kInputDirPath, fn);
    return ZX_OK;
  }
  fd.reset(raw_fd);

  int proto = INPUT_PROTO_NONE;
  if (ioctl_input_get_protocol(fd.get(), &proto) < 0) {
    fprintf(stderr, "Failed to get input device protocol.\n");
    return ZX_ERR_INVALID_ARGS;
  }

  // If the device isn't a keyboard, just continue.
  if (proto != INPUT_PROTO_KBD) {
    return ZX_OK;
  }

  fbl::AllocChecker ac;
  auto keyboard = fbl::make_unique_checked<HidInputDevice>(
      &ac, input_dispatcher_, fbl::move(fd));
  if (!ac.check()) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t status = keyboard->Start();
  if (status != ZX_OK) {
    fprintf(stderr, "Failed to start device %s/%s\n", kInputDirPath, fn);
    return status;
  }
  fprintf(stderr, "hid-device: Polling device %s/%s for key events.\n",
          kInputDirPath, fn);

  fbl::AutoLock lock(&mutex_);
  devices_.push_front(fbl::move(keyboard));
  return ZX_OK;
}

}  // namespace machina