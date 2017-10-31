// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "server.h"

#include <fbl/function.h>

#include "garnet/drivers/bluetooth/lib/att/database.h"
#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"
#include "garnet/drivers/bluetooth/lib/common/uuid.h"
#include "lib/fxl/logging.h"

#include "gatt.h"

namespace bluetooth {
namespace gatt {

Server::Server(fxl::RefPtr<att::Database> database,
               fxl::RefPtr<att::Bearer> bearer)
    : db_(database), att_(bearer) {
  FXL_DCHECK(db_);
  FXL_DCHECK(att_);

  exchange_mtu_id_ = att_->RegisterTransactionHandler(
      att::kExchangeMTURequest, fbl::BindMember(this, &Server::OnExchangeMTU));
  read_by_group_type_id_ = att_->RegisterTransactionHandler(
      att::kReadByGroupTypeRequest,
      fbl::BindMember(this, &Server::OnReadByGroupType));
}

Server::~Server() {
  att_->UnregisterHandler(read_by_group_type_id_);
  att_->UnregisterHandler(exchange_mtu_id_);
}

void Server::OnExchangeMTU(att::Bearer::TransactionId tid,
                           const att::PacketReader& packet) {
  FXL_DCHECK(packet.opcode() == att::kExchangeMTURequest);

  if (packet.payload_size() != sizeof(att::ExchangeMTURequestParams)) {
    att_->ReplyWithError(tid, att::kInvalidHandle, att::ErrorCode::kInvalidPDU);
    return;
  }

  const auto& params = packet.payload<att::ExchangeMTURequestParams>();
  uint16_t client_mtu = le16toh(params.client_rx_mtu);
  uint16_t server_mtu = att_->preferred_mtu();

  auto buffer = common::NewSlabBuffer(sizeof(att::Header) +
                                      sizeof(att::ExchangeMTURequestParams));
  FXL_CHECK(buffer);

  att::PacketWriter writer(att::kExchangeMTUResponse, buffer.get());
  auto rsp_params = writer.mutable_payload<att::ExchangeMTUResponseParams>();
  rsp_params->server_rx_mtu = htole16(server_mtu);

  att_->EndTransaction(tid, std::move(buffer));

  // If the minimum value is less than the default MTU, then go with the default
  // MTU (Vol 3, Part F, 3.4.2.2).
  // TODO(armansito): This needs to use on kBREDRMinATTMTU for BR/EDR. Make the
  // default MTU configurable.
  att_->set_mtu(std::max(att::kLEMinMTU, std::min(client_mtu, server_mtu)));
}

void Server::OnReadByGroupType(att::Bearer::TransactionId tid,
                               const att::PacketReader& packet) {
  FXL_DCHECK(packet.opcode() == att::kReadByGroupTypeRequest);

  att::Handle start, end;
  common::UUID group_type;

  // The group type is represented as either a 16-bit or 128-bit UUID.
  if (packet.payload_size() == sizeof(att::ReadByTypeRequestParams16)) {
    const auto& params = packet.payload<att::ReadByTypeRequestParams16>();
    start = le16toh(params.start_handle);
    end = le16toh(params.end_handle);
    group_type = common::UUID(le16toh(params.type));
  } else if (packet.payload_size() == sizeof(att::ReadByTypeRequestParams128)) {
    const auto& params = packet.payload<att::ReadByTypeRequestParams128>();
    start = le16toh(params.start_handle);
    end = le16toh(params.end_handle);
    group_type = common::UUID(params.type);
  } else {
    att_->ReplyWithError(tid, att::kInvalidHandle, att::ErrorCode::kInvalidPDU);
    return;
  }

  if (group_type != kPrimaryServiceGroupType &&
      group_type != kSecondaryServiceGroupType) {
    att_->ReplyWithError(tid, start, att::ErrorCode::kUnsupportedGroupType);
    return;
  }

  constexpr size_t kRspStructSize = sizeof(att::ReadByGroupTypeResponseParams);
  constexpr size_t kHeaderSize = sizeof(att::Header) + kRspStructSize;
  FXL_DCHECK(kHeaderSize <= att_->mtu());

  std::list<att::AttributeGrouping*> results;
  auto error_code = db_->ReadByGroupType(start, end, group_type,
                                         att_->mtu() - kHeaderSize, &results);
  if (error_code != att::ErrorCode::kNoError) {
    att_->ReplyWithError(tid, start, error_code);
    return;
  }

  FXL_DCHECK(!results.empty());

  // We calculate the size of the response PDU based on the first group
  // declaration value in |results|.
  const size_t kMaxValueSize =
      std::min(att_->mtu() - kHeaderSize - sizeof(att::AttributeGroupDataEntry),
               static_cast<size_t>(att::kMaxReadByGroupTypeValueLength));
  size_t value_size = results.front()->decl_value().size();
  if (results.size() == 1) {
    value_size = std::min(value_size, kMaxValueSize);
  } else {
    FXL_DCHECK(value_size <= kMaxValueSize);
  }

  size_t entry_size = sizeof(att::AttributeGroupDataEntry) + value_size;
  FXL_DCHECK(entry_size <= std::numeric_limits<uint8_t>::max());

  size_t pdu_size = kHeaderSize + entry_size * results.size();
  FXL_DCHECK(pdu_size <= att_->mtu());

  auto buffer = common::NewSlabBuffer(pdu_size);
  FXL_CHECK(buffer);

  att::PacketWriter writer(att::kReadByGroupTypeResponse, buffer.get());
  auto params = writer.mutable_payload<att::ReadByGroupTypeResponseParams>();
  params->length = static_cast<uint8_t>(entry_size);

  // |out_entries| initially references |params->attribute_data_list|. The loop
  // below modifies it as entries are written into the list.
  auto out_entries = writer.mutable_payload_data().mutable_view(kRspStructSize);
  for (const auto& group : results) {
    auto* entry = reinterpret_cast<att::AttributeGroupDataEntry*>(
        out_entries.mutable_data());
    entry->start_handle = htole16(group->start_handle());
    entry->group_end_handle = htole16(group->end_handle());
    out_entries.Write(group->decl_value().view(0, value_size),
                      sizeof(att::AttributeGroupDataEntry));

    // Advance.
    out_entries = out_entries.mutable_view(entry_size);
  }

  att_->EndTransaction(tid, std::move(buffer));
}

}  // namespace gatt
}  // namespace bluetooth
