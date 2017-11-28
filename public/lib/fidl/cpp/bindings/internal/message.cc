// Copyright 2013 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings/message.h"

#include <stdlib.h>

#include <algorithm>

#include "lib/fxl/logging.h"

namespace fidl {

Message::Message() {
  Initialize();
}

Message::~Message() {
  FreeDataAndCloseHandles();
}

void Message::Reset() {
  FreeDataAndCloseHandles();

  handles_.clear();
  Initialize();
}

void Message::AllocData(uint32_t num_bytes) {
  FXL_DCHECK(!data_);
  data_num_bytes_ = num_bytes;
  data_ = static_cast<internal::MessageData*>(calloc(num_bytes, 1));
}

void Message::AllocUninitializedData(uint32_t num_bytes) {
  FXL_DCHECK(!data_);
  data_num_bytes_ = num_bytes;
  data_ = static_cast<internal::MessageData*>(malloc(num_bytes));
}

void Message::MoveTo(Message* destination) {
  FXL_DCHECK(this != destination);

  destination->FreeDataAndCloseHandles();

  // No copy needed.
  destination->data_num_bytes_ = data_num_bytes_;
  destination->data_ = data_;
  std::swap(destination->handles_, handles_);

  handles_.clear();
  Initialize();
}

void Message::Initialize() {
  data_num_bytes_ = 0;
  data_ = nullptr;
}

void Message::FreeDataAndCloseHandles() {
  free(data_);

  for (std::vector<zx_handle_t>::iterator it = handles_.begin();
       it != handles_.end(); ++it) {
    if (*it)
      zx_handle_close(*it);
  }
}

zx_status_t ReadMessage(const zx::channel& channel, Message* message) {
  FXL_DCHECK(channel);
  FXL_DCHECK(message);
  FXL_DCHECK(message->handles()->empty());
  FXL_DCHECK(message->data_num_bytes() == 0);

  uint32_t num_bytes = 0;
  uint32_t num_handles = 0;
  zx_status_t rv =
  channel.read(0, nullptr, 0, &num_bytes, nullptr, 0, &num_handles);
  if (rv != ZX_ERR_BUFFER_TOO_SMALL)
    return rv;

  message->AllocUninitializedData(num_bytes);
  message->mutable_handles()->resize(num_handles);

  uint32_t num_bytes_actual = num_bytes;
  uint32_t num_handles_actual = num_handles;
  rv = channel.read(0, message->mutable_data(), num_bytes, &num_bytes_actual,
                   message->mutable_handles()->empty()
                       ? nullptr
                       : reinterpret_cast<zx_handle_t*>(
                             &message->mutable_handles()->front()),
                   num_handles, &num_handles_actual);

  FXL_DCHECK(num_bytes == num_bytes_actual);
  FXL_DCHECK(num_handles == num_handles_actual);

  return rv;
}

zx_status_t ReadAndDispatchMessage(const zx::channel& channel,
                                   MessageReceiver* receiver,
                                   bool* receiver_result) {
  Message message;
  zx_status_t rv = ReadMessage(channel, &message);
  if (receiver && rv == ZX_OK)
    *receiver_result = receiver->Accept(&message);

  return rv;
}

zx_status_t WriteMessage(const zx::channel& channel, Message* message) {
  FXL_DCHECK(channel);
  FXL_DCHECK(message);

  zx_status_t status = channel.write(
      0, message->data(), message->data_num_bytes(),
      message->mutable_handles()->empty()
          ? nullptr
          : reinterpret_cast<const zx_handle_t*>(
            message->mutable_handles()->data()),
      static_cast<uint32_t>(message->mutable_handles()->size()));

  if (status == ZX_OK) {
    // The handles were successfully transferred, so we don't need the message
    // to track their lifetime any longer.
    message->mutable_handles()->clear();
  }

  return status;
}

zx_status_t CallMessage(const zx::channel& channel, Message* message,
                        Message* response) {
  // TODO(abarth): Once we convert to the FIDL2 wire format, switch this code
  // to use zx_channel_call.

  FXL_DCHECK(response);
  zx_status_t status = WriteMessage(channel, message);
  if (status != ZX_OK)
    return status;

  zx_signals_t observed;
  status = channel.wait_one(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                            ZX_TIME_INFINITE, &observed);

  if (status != ZX_OK)
    return status;

  if (observed & ZX_CHANNEL_READABLE)
    return ReadMessage(channel, response);

  FXL_DCHECK(observed & ZX_CHANNEL_PEER_CLOSED);
  return ZX_ERR_PEER_CLOSED;
}

}  // namespace fidl
