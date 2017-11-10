// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <list>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "garnet/drivers/bluetooth/lib/gap/gap.h"
#include "garnet/drivers/bluetooth/lib/hci/command_channel.h"
#include "garnet/drivers/bluetooth/lib/hci/control_packets.h"
#include "garnet/drivers/bluetooth/lib/hci/low_energy_connector.h"

#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace bluetooth {

namespace hci {
class Connection;
class LowEnergyConnector;
class Transport;
}  // namespace hci

namespace l2cap {
class ChannelManager;
}  // namespace l2cap

namespace gap {

// TODO(armansito): Document the usage pattern.

class LowEnergyConnectionManager;
class RemoteDevice;
class RemoteDeviceCache;

class LowEnergyConnectionRef final {
 public:
  // Destroying this object releases its reference to the underlying connection.
  ~LowEnergyConnectionRef();

  // Releases this object's reference to the underlying connection.
  void Release();

  // Returns true if the underlying connection is still active.
  bool active() const { return active_; }

  // Sets a callback to be called when the underlying connection is closed.
  void set_closed_callback(const fxl::Closure& callback) {
    closed_cb_ = callback;
  }

  const std::string& device_identifier() const { return device_id_; }

 private:
  friend class LowEnergyConnectionManager;

  LowEnergyConnectionRef(const std::string& device_id,
                         fxl::WeakPtr<LowEnergyConnectionManager> manager);

  // Called by LowEnergyConnectionManager when the underlying connection is
  // closed. Notifies |closed_cb_|.
  void MarkClosed();

  bool active_;
  std::string device_id_;
  fxl::WeakPtr<LowEnergyConnectionManager> manager_;
  fxl::Closure closed_cb_;
  fxl::ThreadChecker thread_checker_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyConnectionRef);
};

using LowEnergyConnectionRefPtr = std::unique_ptr<LowEnergyConnectionRef>;

class LowEnergyConnectionManager final {
 public:
  LowEnergyConnectionManager(Mode mode,
                             fxl::RefPtr<hci::Transport> hci,
                             RemoteDeviceCache* device_cache,
                             l2cap::ChannelManager* l2cap);
  ~LowEnergyConnectionManager();

  // Allows a caller to claim shared ownership over a connection to the
  // requested remote LE device identified by |device_identifier|. Returns
  // false, if |device_identifier| is not recognized, otherwise:
  //
  //   * If the requested device is already connected, this method
  //     asynchronously returns a LowEnergyConnectionRef without sending any
  //     requests to the controller. This is done for both local and remote
  //     initiated connections (i.e. the local adapter can either be in the LE
  //     central or peripheral roles). |callback| always succeeds.
  //
  //   * If the requested device is NOT connected, then this method initiates a
  //     connection to the requested device using one of the GAP central role
  //     connection establishment procedures described in Core Spec v5.0, Vol 3,
  //     Part C, Section 9.3. A LowEnergyConnectionRef is asynchronously
  //     returned to the caller once the connection has been set up.
  //
  //     The status of the procedure is reported in |callback| in the case of an
  //     error.
  //
  // |callback| is posted on the creation thread's task runner.
  using ConnectionResultCallback =
      std::function<void(hci::Status, LowEnergyConnectionRefPtr)>;
  bool Connect(const std::string& device_identifier,
               const ConnectionResultCallback& callback);

  // Disconnects any existing LE connection to |device_identifier|, invalidating
  // all active LowEnergyConnectionRefs. Returns false if |device_identifier| is
  // not recognized or the corresponding remote device is not connected.
  bool Disconnect(const std::string& device_identifier);

  // A connection listener can be used to be notified when a connection is
  // established to any remote LE device.
  //
  // |callback| is posted on the creation thread's task runner.
  using ListenerId = size_t;
  using ConnectionCallback = std::function<void(LowEnergyConnectionRefPtr)>;
  ListenerId AddListener(const ConnectionCallback& callback);
  void RemoveListener(ListenerId id);

  // TODO(armansito): Add a RemoteDeviceCache::Observer interface and move these
  // callbacks there.

  // Called when the connection parameters on a link have been updated.
  using ConnectionParametersCallback = std::function<void(const RemoteDevice&)>;
  void SetConnectionParametersCallbackForTesting(
      const ConnectionParametersCallback& callback);

  // Called when a link with the given handle gets disconnected. This event is
  // guaranteed to be called before invalidating connection references.
  // |callback| is run on the creation thread.
  //
  // NOTE: This is intended ONLY for unit tests. Clients should watch for
  // disconnection events using LowEnergyConnectionRef::set_closed_callback()
  // instead. DO NOT use outside of tests.
  using DisconnectCallback = std::function<void(hci::ConnectionHandle)>;
  void SetDisconnectCallbackForTesting(const DisconnectCallback& callback);

  // Sets the timeout interval to be used on future connect requests. The
  // default value is kLECreateConnecionTimeoutMs.
  void set_request_timeout_for_testing(int64_t value_ms) {
    request_timeout_ms_ = value_ms;
  }

 private:
  friend class LowEnergyConnectionRef;

  struct ConnectionState {
    ConnectionState() = default;
    ~ConnectionState() = default;

    ConnectionState(ConnectionState&&) = default;
    ConnectionState& operator=(ConnectionState&&) = default;

    std::string device_id;
    std::unique_ptr<hci::Connection> conn;
    std::unordered_set<LowEnergyConnectionRef*> refs;

    // Marks all references to this connection as closed.
    void CloseRefs();

   private:
    FXL_DISALLOW_COPY_AND_ASSIGN(ConnectionState);
  };

  // Mapping from device identifiers to open LE connections.
  using ConnectionStateMap = std::unordered_map<std::string, ConnectionState>;

  class PendingRequestData {
   public:
    PendingRequestData(const common::DeviceAddress& address,
                       const ConnectionResultCallback& first_callback);
    PendingRequestData() = default;
    ~PendingRequestData() = default;

    PendingRequestData(PendingRequestData&&) = default;
    PendingRequestData& operator=(PendingRequestData&&) = default;

    void AddCallback(const ConnectionResultCallback& cb) {
      callbacks_.push_back(cb);
    }

    // Notifies all elements in |callbacks| with |status| and the result of
    // |func|.
    using RefFunc = std::function<LowEnergyConnectionRefPtr()>;
    void NotifyCallbacks(hci::Status status, const RefFunc& func);

    const common::DeviceAddress& address() const { return address_; }

   private:
    common::DeviceAddress address_;
    std::list<ConnectionResultCallback> callbacks_;

    FXL_DISALLOW_COPY_AND_ASSIGN(PendingRequestData);
  };

  // Called by LowEnergyConnectionRef::Release().
  void ReleaseReference(LowEnergyConnectionRef* conn_ref);

  // Called when |connector_| completes a pending request. Initiates a new
  // connection attempt for the next device in the pending list, if any.
  void TryCreateNextConnection();

  // Initiates a connection attempt to |peer|.
  void RequestCreateConnection(RemoteDevice* peer);

  // Initializes the connection state for the device with the given identifier
  // and returns the initial reference.
  LowEnergyConnectionRefPtr InitializeConnection(
      const std::string& device_identifier,
      std::unique_ptr<hci::Connection> connection);

  // Adds a new connection reference to an existing connection to the device
  // with the ID |device_identifier| and returns it. Returns nullptr if
  // |device_identifier| is not recognized.
  LowEnergyConnectionRefPtr AddConnectionRef(
      const std::string& device_identifier);

  // Cleans up a connection state. This result in a HCI_Disconnect command (if
  // the connection is marked as open) and notifies any referenced
  // LowEnergyConnectionRefs of the disconnection.
  //
  // This is also responsible for unregistering the link from managed subsystems
  // (e.g. L2CAP).
  void CleanUpConnectionState(ConnectionState* conn_state);

  // Called by |connector_| when a new LE connection has been created.
  void OnConnectionCreated(std::unique_ptr<hci::Connection> connection);

  // Called by |connector_| to indicate the result of a connect request.
  void OnConnectResult(const std::string& device_identifier,
                       hci::LowEnergyConnector::Result result,
                       hci::Status status);

  // Event handler for the HCI Disconnection Complete event.
  // TODO(armansito): This needs to be shared between the BR/EDR and LE
  // connection managers, so this handler should be moved elsewhere.
  void OnDisconnectionComplete(const hci::EventPacket& event);

  // Event handler for the HCI LE Connection Update Complete event.
  void OnLEConnectionUpdateComplete(const hci::EventPacket& event);

  // Called when the preferred connection parameters have been received for a LE
  // peripheral. This can happen in the form of:
  //
  //   1. <<Slave Connection Interval Range>> advertising data field
  //   2. "Peripheral Preferred Connection Parameters" GATT characteristic
  //      (under "GAP" service)
  //   3. HCI LE Remote Connection Parameter Request Event
  //   4. L2CAP Connection Parameter Update request
  //
  // TODO(armansito): Support #1, #2, and #3 above.
  //
  // This method caches |params| for later connection attempts and sends the
  // parameters to the controller if the initializing procedures are complete
  // (since we use more agressing initial parameters for pairing and service
  // discovery, as recommended by the specification in v5.0, Vol 3, Part C,
  // Section 9.3.12.1).
  //
  // |device_identifier| uniquely identifies the peer. |handle| represents the
  // the logical link that |params| should be applied to.
  void OnNewLEConnectionParams(
      const std::string& device_identifier,
      hci::ConnectionHandle handle,
      const hci::LEPreferredConnectionParameters& params);

  // Tells the controller to use the given connection |params| on the given
  // logical link |handle|.
  void UpdateConnectionParams(
      hci::ConnectionHandle handle,
      const hci::LEPreferredConnectionParameters& params);

  // Returns an interator into |connections_| if a ConnectionState is found that
  // matches the given logical link |handle|. Otherwise, returns an iterator
  // that is equal to |connections_.end()|.
  //
  // The general rules of validity around std::unordered_map::iterator apply to
  // the returned value.
  ConnectionStateMap::iterator FindConnectionStateIter(
      hci::ConnectionHandle handle);

  fxl::RefPtr<hci::Transport> hci_;

  // Time after which a connection attempt is considered to have timed out. This
  // is configurable to allow unit tests to set a shorter value.
  int64_t request_timeout_ms_;

  // The task runner for all asynchronous tasks.
  fxl::RefPtr<fxl::TaskRunner> task_runner_;

  // The device cache is used to look up and persist remote device data that is
  // relevant during connection establishment (such as the address, preferred
  // connetion parameters, etc). Expected to outlive this instance.
  RemoteDeviceCache* device_cache_;  // weak

  // The L2CAP layer is shared between the BR/EDR and LE connection managers and
  // it is expected to out-live both. Expected to outlive this instance.
  l2cap::ChannelManager* l2cap_;  // weak

  // Event handler ID for the HCI Disconnection Complete event.
  hci::CommandChannel::EventHandlerId disconn_cmpl_handler_id_;

  // Event handler ID for the HCI LE Connection Update Complete event.
  hci::CommandChannel::EventHandlerId conn_update_cmpl_handler_id_;

  // Callbacks used by unit tests to observe connection state events.
  ConnectionParametersCallback test_conn_params_cb_;
  DisconnectCallback test_disconn_cb_;

  ListenerId next_listener_id_;
  std::unordered_map<ListenerId, ConnectionCallback> listeners_;

  // Outstanding connection requests based on remote device ID.
  std::unordered_map<std::string, PendingRequestData> pending_requests_;

  // Mapping from device identifiers to currently open LE connections.
  ConnectionStateMap connections_;

  // Performs the Direct Connection Establishment procedure.
  std::unique_ptr<hci::LowEnergyConnector> connector_;

  // Keep this as the last member to make sure that all weak pointers are
  // invalidated before other members get destroyed.
  fxl::WeakPtrFactory<LowEnergyConnectionManager> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(LowEnergyConnectionManager);
};

}  // namespace gap
}  // namespace bluetooth
