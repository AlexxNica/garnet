// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "channel_manager.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "logical_link.h"

namespace btlib {
namespace l2cap {

ChannelManager::ChannelManager(fxl::RefPtr<hci::Transport> hci,
                               fxl::RefPtr<fxl::TaskRunner> task_runner)
    : hci_(hci), task_runner_(task_runner) {
  FXL_DCHECK(hci_);
  FXL_DCHECK(task_runner_);

  hci_->acl_data_channel()->SetDataRxHandler(std::bind(
      &ChannelManager::OnACLDataReceived, this, std::placeholders::_1));
}

ChannelManager::~ChannelManager() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  cancelable_callback_factory_.CancelAll();
  hci_->acl_data_channel()->SetDataRxHandler({});
}

void ChannelManager::Register(hci::ConnectionHandle handle,
                              hci::Connection::LinkType ll_type,
                              hci::Connection::Role role) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  std::lock_guard<std::mutex> lock(mtx_);
  RegisterInternalLocked(handle, ll_type, role);
}

void ChannelManager::RegisterLE(
    hci::ConnectionHandle handle,
    hci::Connection::Role role,
    const LEConnectionParameterUpdateCallback& callback,
    fxl::RefPtr<fxl::TaskRunner> task_runner) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  std::lock_guard<std::mutex> lock(mtx_);
  auto* ll =
      RegisterInternalLocked(handle, hci::Connection::LinkType::kLE, role);
  ll->le_signaling_channel()->set_conn_param_update_callback(callback,
                                                             task_runner);
}

void ChannelManager::Unregister(hci::ConnectionHandle handle) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  std::lock_guard<std::mutex> lock(mtx_);

  auto iter = ll_map_.find(handle);
  FXL_DCHECK(iter != ll_map_.end()) << fxl::StringPrintf(
      "l2cap: Attempted to remove unknown connection handle: 0x%04x", handle);
  ll_map_.erase(iter);
  pending_packets_.erase(handle);
}

std::unique_ptr<Channel> ChannelManager::OpenFixedChannel(
    hci::ConnectionHandle handle,
    ChannelId channel_id) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  std::lock_guard<std::mutex> lock(mtx_);

  auto iter = ll_map_.find(handle);
  if (iter == ll_map_.end()) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
        "l2cap: Cannot open fixed channel on unknown connection handle: 0x%04x",
        handle);
    return nullptr;
  }

  return iter->second->OpenFixedChannel(channel_id);
}

void ChannelManager::OnACLDataReceived(hci::ACLDataPacketPtr packet) {
  // The creation thread of this object is expected to be different from the HCI
  // I/O thread.
  FXL_DCHECK(hci_->io_task_runner()->RunsTasksOnCurrentThread());

  // TODO(armansito): Route packets based on channel priority, prioritizing
  // Guaranteed channels over Best Effort. Right now all channels are Best
  // Effort.

  auto handle = packet->connection_handle();

  std::lock_guard<std::mutex> lock(mtx_);

  auto iter = ll_map_.find(handle);
  PendingPacketMap::iterator pp_iter;

  // If a LogicalLink does not exist, we buffer its packets to be delivered when
  // the LogicalLink gets created. If a LogicalLink DOES exist, we conditionally
  // buffer it depending on whether the drain task has run (see
  // ChannelManager::Register() above).

  if (iter == ll_map_.end()) {
    pp_iter = pending_packets_
                  .emplace(handle, common::LinkedList<hci::ACLDataPacket>())
                  .first;
  } else {
    pp_iter = pending_packets_.find(handle);
  }

  if (pp_iter != pending_packets_.end()) {
    pp_iter->second.push_back(std::move(packet));

    FXL_VLOG(1) << fxl::StringPrintf(
        "l2cap: Queued rx packet on handle: 0x%04x", handle);
    return;
  }

  // NOTE: |mtx_| may remain locked until the packet is pushed over to the
  // channel's rx data handler. It is important that LogicalLink make no calls
  // to ChannelManager's public methods in this context.
  // TODO(armansito): We should improve this once we support L2CAP modes other
  // than basic mode and if we add more threads for data handling. This can be
  // especially problematic if the target Channel's mode implementation does any
  // long-running computation, which would cause the thread calling
  // Register/Unregister to potentially block for a long time (not to mention
  // data coming in over other threads, if we add them).
  // TODO(armansito): It's probably OK to keep shared_ptrs to LogicalLink and to
  // temporarily add a ref before calling HandleRxPacket on it. Revisit later.
  iter->second->HandleRxPacket(std::move(packet));
}

internal::LogicalLink* ChannelManager::RegisterInternalLocked(
    hci::ConnectionHandle handle,
    hci::Connection::LinkType ll_type,
    hci::Connection::Role role) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  auto iter = ll_map_.find(handle);
  FXL_DCHECK(iter == ll_map_.end()) << fxl::StringPrintf(
      "l2cap: Connection registered more than once! (handle=0x%04x)", handle);

  auto ll =
      std::make_unique<internal::LogicalLink>(handle, ll_type, role, hci_);
  auto* ret = ll.get();
  ll_map_[handle] = std::move(ll);

  // Handle pending packets on the link, if any.
  auto pp_iter = pending_packets_.find(handle);
  if (pp_iter != pending_packets_.end()) {
    hci_->io_task_runner()->PostTask(
        cancelable_callback_factory_.MakeTask([this, handle] {
          std::lock_guard<std::mutex> lock(mtx_);

          // First check that |handle| is still there
          auto iter = ll_map_.find(handle);
          if (iter == ll_map_.end())
            return;

          auto pp_iter = pending_packets_.find(handle);
          FXL_DCHECK(pp_iter != pending_packets_.end());

          auto& ll = iter->second;
          auto& packets = pp_iter->second;
          while (!packets.is_empty()) {
            ll->HandleRxPacket(packets.pop_front());
          }
          pending_packets_.erase(pp_iter);
        }));
  }

  return ret;
}

}  // namespace l2cap
}  // namespace btlib
