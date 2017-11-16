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

namespace btlib {
namespace gatt {

Server::Server(fxl::RefPtr<att::Database> database,
               fxl::RefPtr<att::Bearer> bearer)
    : db_(database), att_(bearer), weak_ptr_factory_(this) {
  FXL_DCHECK(db_);
  FXL_DCHECK(att_);

  exchange_mtu_id_ = att_->RegisterTransactionHandler(
      att::kExchangeMTURequest, fbl::BindMember(this, &Server::OnExchangeMTU));
  find_information_id_ = att_->RegisterTransactionHandler(
      att::kFindInformationRequest,
      fbl::BindMember(this, &Server::OnFindInformation));
  read_by_group_type_id_ = att_->RegisterTransactionHandler(
      att::kReadByGroupTypeRequest,
      fbl::BindMember(this, &Server::OnReadByGroupType));
  read_by_type_id_ = att_->RegisterTransactionHandler(
      att::kReadByTypeRequest, fbl::BindMember(this, &Server::OnReadByType));
}

Server::~Server() {
  att_->UnregisterHandler(read_by_type_id_);
  att_->UnregisterHandler(read_by_group_type_id_);
  att_->UnregisterHandler(find_information_id_);
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

void Server::OnFindInformation(att::Bearer::TransactionId tid,
                               const att::PacketReader& packet) {
  FXL_DCHECK(packet.opcode() == att::kFindInformationRequest);

  if (packet.payload_size() != sizeof(att::FindInformationRequestParams)) {
    att_->ReplyWithError(tid, att::kInvalidHandle, att::ErrorCode::kInvalidPDU);
    return;
  }

  const auto& params = packet.payload<att::FindInformationRequestParams>();
  att::Handle start = le16toh(params.start_handle);
  att::Handle end = le16toh(params.end_handle);

  constexpr size_t kRspStructSize = sizeof(att::FindInformationResponseParams);
  constexpr size_t kHeaderSize = sizeof(att::Header) + kRspStructSize;
  FXL_DCHECK(kHeaderSize <= att_->mtu());

  std::list<const att::Attribute*> results;
  auto error_code =
      db_->FindInformation(start, end, att_->mtu() - kHeaderSize, &results);
  if (error_code != att::ErrorCode::kNoError) {
    att_->ReplyWithError(tid, start, error_code);
    return;
  }

  FXL_DCHECK(!results.empty());

  size_t entry_size = sizeof(att::Handle) + results.front()->type().CompactSize(
                                                false /* allow32_bit */);
  size_t pdu_size = kHeaderSize + entry_size * results.size();

  auto buffer = common::NewSlabBuffer(pdu_size);
  FXL_CHECK(buffer);

  att::PacketWriter writer(att::kFindInformationResponse, buffer.get());
  auto rsp_params =
      writer.mutable_payload<att::FindInformationResponseParams>();
  rsp_params->format =
      (entry_size == 4) ? att::UUIDType::k16Bit : att::UUIDType::k128Bit;

  // |out_entries| initially references |params->information_data|. The loop
  // below modifies it as entries are written into the list.
  auto out_entries = writer.mutable_payload_data().mutable_view(kRspStructSize);
  for (const auto& attr : results) {
    *reinterpret_cast<att::Handle*>(out_entries.mutable_data()) =
        htole16(attr->handle());
    auto uuid_view = out_entries.mutable_view(sizeof(att::Handle));
    attr->type().ToBytes(&uuid_view, false /* allow32_bit */);

    // advance
    out_entries = out_entries.mutable_view(entry_size);
  }

  att_->EndTransaction(tid, std::move(buffer));
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

void Server::OnReadByType(att::Bearer::TransactionId tid,
                          const att::PacketReader& packet) {
  FXL_DCHECK(packet.opcode() == att::kReadByTypeRequest);

  att::Handle start, end;
  common::UUID type;

  // The attribute type is represented as either a 16-bit or 128-bit UUID.
  if (packet.payload_size() == sizeof(att::ReadByTypeRequestParams16)) {
    const auto& params = packet.payload<att::ReadByTypeRequestParams16>();
    start = le16toh(params.start_handle);
    end = le16toh(params.end_handle);
    type = common::UUID(le16toh(params.type));
  } else if (packet.payload_size() == sizeof(att::ReadByTypeRequestParams128)) {
    const auto& params = packet.payload<att::ReadByTypeRequestParams128>();
    start = le16toh(params.start_handle);
    end = le16toh(params.end_handle);
    type = common::UUID(params.type);
  } else {
    att_->ReplyWithError(tid, att::kInvalidHandle, att::ErrorCode::kInvalidPDU);
    return;
  }

  constexpr size_t kRspStructSize = sizeof(att::ReadByTypeResponseParams);
  constexpr size_t kHeaderSize = sizeof(att::Header) + kRspStructSize;
  FXL_DCHECK(kHeaderSize <= att_->mtu());

  std::list<const att::Attribute*> results;
  auto error_code =
      db_->ReadByType(start, end, type, att_->mtu() - kHeaderSize, &results);
  if (error_code != att::ErrorCode::kNoError) {
    att_->ReplyWithError(tid, start, error_code);
    return;
  }

  FXL_DCHECK(!results.empty());

  const size_t kMaxValueSize =
      std::min(att_->mtu() - kHeaderSize - sizeof(att::AttributeData),
               static_cast<size_t>(att::kMaxReadByTypeValueLength));

  // If the value is dynamic, then delegate the read to any registered handler.
  if (!results.front()->value()) {
    FXL_DCHECK(results.size() == 1u);

    att::Handle handle = results.front()->handle();
    auto self = weak_ptr_factory_.GetWeakPtr();
    auto result_cb = [self, tid, handle, kMaxValueSize, kHeaderSize](
                         att::ErrorCode ecode, const auto& value) {
      if (!self)
        return;

      if (ecode != att::ErrorCode::kNoError) {
        self->att_->ReplyWithError(tid, handle, ecode);
        return;
      }

      // Respond with just a single entry.
      size_t value_size = std::min(value.size(), kMaxValueSize);
      size_t entry_size = value_size + sizeof(att::AttributeData);
      auto buffer = common::NewSlabBuffer(entry_size + kHeaderSize);
      att::PacketWriter writer(att::kReadByTypeResponse, buffer.get());

      auto params = writer.mutable_payload<att::ReadByTypeResponseParams>();
      params->length = static_cast<uint8_t>(entry_size);
      params->attribute_data_list->handle = htole16(handle);
      writer.mutable_payload_data().Write(
          value.data(), value_size,
          sizeof(params->length) + sizeof(handle));

      self->att_->EndTransaction(tid, std::move(buffer));
    };

    // Respond with an error if no read handler was registered.
    if (!results.front()->ReadAsync(0, result_cb)) {
      att_->ReplyWithError(tid, handle, att::ErrorCode::kReadNotPermitted);
    }
    return;
  }

  // We calculate the size of the response PDU based on the first attribute
  // value.
  size_t value_size = results.front()->value()->size();
  if (results.size() == 1) {
    value_size = std::min(value_size, kMaxValueSize);
  } else {
    FXL_DCHECK(value_size <= kMaxValueSize);
  }

  size_t entry_size = sizeof(att::AttributeData) + value_size;
  FXL_DCHECK(entry_size <= std::numeric_limits<uint8_t>::max());

  size_t pdu_size = kHeaderSize + entry_size * results.size();
  FXL_DCHECK(pdu_size <= att_->mtu());

  auto buffer = common::NewSlabBuffer(pdu_size);
  FXL_CHECK(buffer);

  att::PacketWriter writer(att::kReadByTypeResponse, buffer.get());
  auto params = writer.mutable_payload<att::ReadByTypeResponseParams>();
  params->length = static_cast<uint8_t>(entry_size);

  // |out_entries| initially references |params->attribute_data_list|. The loop
  // below modifies it as entries are written into the list.
  auto out_entries = writer.mutable_payload_data().mutable_view(kRspStructSize);
  for (const auto& attr : results) {
    auto* entry =
        reinterpret_cast<att::AttributeData*>(out_entries.mutable_data());
    entry->handle = htole16(attr->handle());
    out_entries.Write(attr->value()->view(0, value_size),
                      sizeof(entry->handle));

    // Advance.
    out_entries = out_entries.mutable_view(entry_size);
  }

  att_->EndTransaction(tid, std::move(buffer));
}

}  // namespace gatt
}  // namespace btlib
