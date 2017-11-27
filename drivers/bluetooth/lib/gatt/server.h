// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/att/bearer.h"

#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace btlib {

namespace att {
class Database;
class PacketReader;
}  // namespace att

namespace gatt {

// A GATT Server implements the server-role of the ATT protocol over a single
// ATT Bearer. A unique Server instance should exist for each logical link that
// supports GATT.
//
// Each Server responds to incoming requests by querying a common attribute
// database that exists on a particular Adapter. Each Server is handed an
// att::Bearer that represents the logical link that it shares with a GATT
// Client that is responsible for the client-role. Depending on the state of
// each transaction a Server may explicitly shutdown the Bearer.
class Server final {
 public:
  // |database| is the attribute database that this Server will query to resolve
  // its transactions.
  //
  // |bearer| is the ATT data bearer that this Server operates on. Passed as a
  // weak pointer as this is expected to outlive this object.
  Server(fxl::RefPtr<att::Database> database, fxl::RefPtr<att::Bearer> bearer);
  ~Server();

 private:
  // ATT protocol request handlers:
  void OnExchangeMTU(att::Bearer::TransactionId tid,
                     const att::PacketReader& packet);
  void OnFindInformation(att::Bearer::TransactionId tid,
                         const att::PacketReader& packet);
  void OnReadByGroupType(att::Bearer::TransactionId tid,
                         const att::PacketReader& packet);
  void OnReadByType(att::Bearer::TransactionId tid,
                    const att::PacketReader& packet);
  void OnReadRequest(att::Bearer::TransactionId tid,
                     const att::PacketReader& packet);
  void OnWriteRequest(att::Bearer::TransactionId tid,
                      const att::PacketReader& packet);

  fxl::RefPtr<att::Database> db_;
  fxl::RefPtr<att::Bearer> att_;

  // ATT protocol request handler IDs
  att::Bearer::HandlerId exchange_mtu_id_;
  att::Bearer::HandlerId find_information_id_;
  att::Bearer::HandlerId read_by_group_type_id_;
  att::Bearer::HandlerId read_by_type_id_;
  att::Bearer::HandlerId read_req_id_;
  att::Bearer::HandlerId write_req_id_;

  fxl::WeakPtrFactory<Server> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Server);
};

}  // namespace gatt
}  // namespace btlib
