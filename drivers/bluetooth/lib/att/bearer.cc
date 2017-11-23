// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "bearer.h"

#include <zircon/status.h>

#include "garnet/drivers/bluetooth/lib/common/slab_allocator.h"
#include "garnet/drivers/bluetooth/lib/l2cap/channel.h"

#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/strings/string_printf.h"

namespace bluetooth {
namespace att {
namespace {

MethodType GetMethodType(OpCode opcode) {
  // We treat all packets as a command if the command bit was set. An
  // unrecognized command will always be ignored (so it is OK to return kCommand
  // here if, for example, |opcode| is a response with the command-bit set).
  if (opcode & kCommandFlag)
    return MethodType::kCommand;

  switch (opcode) {
    case kInvalidOpCode:
      return MethodType::kInvalid;

    case kExchangeMTURequest:
    case kFindInformationRequest:
    case kFindByTypeValueRequest:
    case kReadByTypeRequest:
    case kReadRequest:
    case kReadBlobRequest:
    case kReadMultipleRequest:
    case kReadByGroupTypeRequest:
    case kWriteRequest:
    case kPrepareWriteRequest:
    case kExecuteWriteRequest:
      return MethodType::kRequest;

    case kErrorResponse:
    case kExchangeMTUResponse:
    case kFindInformationResponse:
    case kFindByTypeValueResponse:
    case kReadByTypeResponse:
    case kReadResponse:
    case kReadBlobResponse:
    case kReadMultipleResponse:
    case kReadByGroupTypeResponse:
    case kWriteResponse:
    case kPrepareWriteResponse:
    case kExecuteWriteResponse:
      return MethodType::kResponse;

    case kNotification:
      return MethodType::kNotification;
    case kIndication:
      return MethodType::kIndication;
    case kConfirmation:
      return MethodType::kConfirmation;

    // These are redundant with the check above but are included for
    // completeness.
    case kWriteCommand:
    case kSignedWriteCommand:
      return MethodType::kCommand;

    default:
      break;
  }

  // Everything else will be treated as an incoming request.
  return MethodType::kRequest;
}

// Returns the corresponding originating transaction opcode for
// |transaction_end_code|, where the latter must correspond to a response or
// confirmation.
OpCode MatchingTransactionCode(OpCode transaction_end_code) {
  switch (transaction_end_code) {
    case kExchangeMTUResponse:
      return kExchangeMTURequest;
    case kFindInformationResponse:
      return kFindInformationRequest;
    case kFindByTypeValueResponse:
      return kFindByTypeValueRequest;
    case kReadByTypeResponse:
      return kReadByTypeRequest;
    case kReadResponse:
      return kReadRequest;
    case kReadBlobResponse:
      return kReadBlobRequest;
    case kReadMultipleResponse:
      return kReadMultipleRequest;
    case kReadByGroupTypeResponse:
      return kReadByGroupTypeRequest;
    case kWriteResponse:
      return kWriteRequest;
    case kPrepareWriteResponse:
      return kPrepareWriteRequest;
    case kExecuteWriteResponse:
      return kExecuteWriteRequest;
    case kConfirmation:
      return kIndication;
    default:
      break;
  }

  return kInvalidOpCode;
}

}  // namespace

Bearer::PendingTransaction::PendingTransaction(
    OpCode opcode,
    TransactionCallback callback,
    ErrorCallback error_callback,
    std::unique_ptr<common::ByteBuffer> pdu)
    : opcode(opcode),
      callback(callback),
      error_callback(error_callback),
      pdu(std::move(pdu)) {
  FXL_DCHECK(this->callback);
  FXL_DCHECK(this->error_callback);
  FXL_DCHECK(this->pdu);
}

Bearer::TransactionQueue::~TransactionQueue() {
  if (timeout_task_)
    CancelTimeout();
}

void Bearer::TransactionQueue::CancelTimeout() {
  FXL_DCHECK(timeout_task_);
  zx_status_t status =
      timeout_task_->Cancel(fsl::MessageLoop::GetCurrent()->async());
  if (status != ZX_OK) {
    FXL_VLOG(2) << "att: timeout task failed: " << zx_status_get_string(status);
  }
  timeout_task_ = nullptr;
}

Bearer::PendingTransactionPtr Bearer::TransactionQueue::ClearCurrent() {
  FXL_DCHECK(current_);
  CancelTimeout();
  return std::move(current_);
}

void Bearer::TransactionQueue::Enqueue(PendingTransactionPtr transaction) {
  queue_.push_back(std::move(transaction));
}

void Bearer::TransactionQueue::TrySendNext(l2cap::Channel* chan,
                                           const fxl::Closure& timeout_cb,
                                           uint32_t timeout_ms) {
  FXL_DCHECK(chan);

  // Abort if a transaction is currently pending.
  if (current())
    return;

  // Advance to the next transaction.
  current_ = queue_.pop_front();
  if (current()) {
    SetTimeout(timeout_cb, timeout_ms);
    chan->Send(std::move(current()->pdu));
  }
}

void Bearer::TransactionQueue::SetTimeout(const fxl::Closure& callback,
                                          uint32_t timeout_ms) {
  FXL_DCHECK(callback);
  FXL_DCHECK(current_);
  FXL_DCHECK(!timeout_task_);

  auto time_delta =
      fxl::TimeDelta::FromMilliseconds(static_cast<int64_t>(timeout_ms));
  auto target_time = fxl::TimePoint::Now() + time_delta;
  auto target_ns = target_time.ToEpochDelta().ToNanoseconds();
  timeout_task_ = std::make_unique<async::Task>(target_ns, 0);
  timeout_task_->set_handler(
      [callback = std::move(callback)](async_t*, zx_status_t status) {
        if (status == ZX_OK)
          callback();
        return ASYNC_TASK_FINISHED;
      });
  timeout_task_->Post(fsl::MessageLoop::GetCurrent()->async());
}

void Bearer::TransactionQueue::Reset() {
  if (timeout_task_) {
    CancelTimeout();
  }

  queue_.clear();
  current_ = nullptr;
}

void Bearer::TransactionQueue::InvokeErrorAll(bool timeout,
                                              ErrorCode error_code) {
  if (current_) {
    current_->error_callback(timeout, error_code, kInvalidHandle);
  }

  for (const auto& t : queue_) {
    if (t.error_callback)
      t.error_callback(timeout, error_code, kInvalidHandle);
  }
}

Bearer::Bearer(std::unique_ptr<l2cap::Channel> chan,
               uint32_t transaction_timeout_ms)
    : chan_(std::move(chan)), transaction_timeout_ms_(transaction_timeout_ms) {
  FXL_DCHECK(chan_);

  if (chan_->link_type() == hci::Connection::LinkType::kLE) {
    min_mtu_ = kLEMinMTU;
  } else {
    min_mtu_ = kBREDRMinMTU;
  }

  mtu_ = min_mtu();
  preferred_mtu_ =
      std::max(min_mtu(), std::min(chan_->tx_mtu(), chan_->rx_mtu()));

  chan_->set_channel_closed_callback(std::bind(&Bearer::OnChannelClosed, this));

  rx_task_.Reset(std::bind(&Bearer::OnRxBFrame, this, std::placeholders::_1));
  chan_->SetRxHandler(rx_task_.callback(),
                      fsl::MessageLoop::GetCurrent()->task_runner());
}

Bearer::~Bearer() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  rx_task_.Cancel();
  chan_ = nullptr;

  request_queue_.Reset();
  indication_queue_.Reset();
}

void Bearer::ShutDown() {
  if (is_open())
    ShutDownInternal(false /* due_to_timeout */);
}

void Bearer::ShutDownInternal(bool due_to_timeout) {
  FXL_DCHECK(is_open());

  FXL_VLOG(1) << "att: Bearer shutting down";

  rx_task_.Cancel();
  chan_ = nullptr;

  request_queue_.InvokeErrorAll(due_to_timeout, ErrorCode::kNoError);
  request_queue_.Reset();
  indication_queue_.InvokeErrorAll(due_to_timeout, ErrorCode::kNoError);
  indication_queue_.Reset();

  if (closed_cb_)
    closed_cb_();
}

bool Bearer::StartTransaction(std::unique_ptr<common::ByteBuffer> pdu,
                              const TransactionCallback& callback,
                              const ErrorCallback& error_callback) {
  FXL_DCHECK(pdu);
  FXL_DCHECK(callback);
  FXL_DCHECK(error_callback);

  return SendInternal(std::move(pdu), callback, error_callback);
}

bool Bearer::SendWithoutResponse(std::unique_ptr<common::ByteBuffer> pdu) {
  FXL_DCHECK(pdu);
  return SendInternal(std::move(pdu), {}, {});
}

bool Bearer::SendInternal(std::unique_ptr<common::ByteBuffer> pdu,
                          const TransactionCallback& callback,
                          const ErrorCallback& error_callback) {
  if (!is_open()) {
    FXL_VLOG(2) << "att: Bearer closed";
    return false;
  }

  if (!IsPacketValid(*pdu)) {
    FXL_VLOG(1) << "att: Packet has bad length!";
    return false;
  }

  PacketReader reader(pdu.get());
  MethodType type = GetMethodType(reader.opcode());

  TransactionQueue* tq = nullptr;

  switch (type) {
    case MethodType::kCommand:
    case MethodType::kNotification:
      if (callback || error_callback) {
        FXL_VLOG(1) << "att: Method not a transaction";
        return false;
      }

      // Send the command. No flow control is necessary.
      chan_->Send(std::move(pdu));
      return true;

    case MethodType::kRequest:
      tq = &request_queue_;
      break;
    case MethodType::kIndication:
      tq = &indication_queue_;
      break;
    default:
      FXL_VLOG(1) << "att: invalid opcode: " << reader.opcode();
      return false;
  }

  if (!callback || !error_callback) {
    FXL_VLOG(1) << "att: Transaction requires callbacks";
    return false;
  }

  tq->Enqueue(std::make_unique<PendingTransaction>(
      reader.opcode(), callback, error_callback, std::move(pdu)));
  TryStartNextTransaction(tq);

  return true;
}

bool Bearer::IsPacketValid(const common::ByteBuffer& packet) {
  return packet.size() != 0u && packet.size() <= mtu_;
}

void Bearer::TryStartNextTransaction(TransactionQueue* tq) {
  FXL_DCHECK(is_open());
  FXL_DCHECK(tq);

  tq->TrySendNext(chan_.get(),
                  [this] { ShutDownInternal(true /* due_to_timeout */); },
                  transaction_timeout_ms_);
}

void Bearer::SendErrorResponse(OpCode request_opcode,
                               Handle attribute_handle,
                               ErrorCode error_code) {
  auto buffer =
      common::NewSlabBuffer(sizeof(Header) + sizeof(ErrorResponseParams));
  FXL_CHECK(buffer);

  PacketWriter packet(kErrorResponse, buffer.get());
  auto* payload = packet.mutable_payload<ErrorResponseParams>();
  payload->request_opcode = request_opcode;
  payload->attribute_handle = htole16(attribute_handle);
  payload->error_code = error_code;

  chan_->Send(std::move(buffer));
}

void Bearer::HandleEndTransaction(TransactionQueue* tq,
                                  const PacketReader& packet) {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  FXL_DCHECK(is_open());
  FXL_DCHECK(tq);

  if (!tq->current()) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "att: received unexpected transaction PDU (opcode: 0x%02x)",
        packet.opcode());
    ShutDown();
    return;
  }

  bool report_error = false;
  OpCode target_opcode;
  ErrorCode error_code = ErrorCode::kNoError;
  Handle attr_in_error = kInvalidHandle;

  if (packet.opcode() == kErrorResponse) {
    // We should never hit this branch for indications.
    FXL_DCHECK(tq->current()->opcode != kIndication);

    if (packet.payload_size() == sizeof(ErrorResponseParams)) {
      report_error = true;

      const auto& payload = packet.payload<ErrorResponseParams>();
      target_opcode = payload.request_opcode;
      error_code = payload.error_code;
      attr_in_error = le16toh(payload.attribute_handle);
    } else {
      FXL_VLOG(2) << "att: Received malformed error response";

      // Invalid opcode will fail the opcode comparison below.
      target_opcode = kInvalidOpCode;
    }
  } else {
    target_opcode = MatchingTransactionCode(packet.opcode());
  }

  FXL_DCHECK(tq->current()->opcode != kInvalidOpCode);

  if (tq->current()->opcode != target_opcode) {
    FXL_VLOG(1) << fxl::StringPrintf(
        "att: Received bad transaction PDU (opcode: 0x%02x)", packet.opcode());
    ShutDown();
    return;
  }

  // The transaction is complete. Send out the next queued transaction and
  // notify the callback.
  auto transaction = tq->ClearCurrent();
  FXL_DCHECK(transaction);

  TryStartNextTransaction(tq);

  if (!report_error)
    transaction->callback(packet);
  else if (transaction->error_callback)
    transaction->error_callback(false /* timeout */, error_code, attr_in_error);
}

void Bearer::OnChannelClosed() {
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());
  ShutDown();

  // TODO(armansito): Notify a "closed callback" here.
}

void Bearer::OnRxBFrame(const l2cap::SDU& sdu) {
  FXL_DCHECK(is_open());
  FXL_DCHECK(thread_checker_.IsCreationThreadCurrent());

  uint16_t length = sdu.length();

  // An ATT PDU should at least contain the opcode.
  if (length < sizeof(OpCode)) {
    FXL_VLOG(1) << "att: PDU too short!";
    ShutDown();
    return;
  }

  if (length > mtu_) {
    FXL_VLOG(1) << "att: PDU exceeds MTU!";
    ShutDown();
    return;
  }

  // The following will read the entire ATT PDU in a single call.
  l2cap::SDU::Reader reader(&sdu);
  reader.ReadNext(length, [this, length](const common::ByteBuffer& att_pdu) {
    FXL_CHECK(att_pdu.size() == length);
    PacketReader packet(&att_pdu);

    MethodType type = GetMethodType(packet.opcode());

    switch (type) {
      case MethodType::kResponse:
        HandleEndTransaction(&request_queue_, packet);
        break;
      case MethodType::kConfirmation:
        HandleEndTransaction(&indication_queue_, packet);
        break;
      default:
        // TODO(armansito): For now we respond with an error to all
        // notifications and indications as well.
        FXL_VLOG(2) << fxl::StringPrintf("att: Unsupported opcode: 0x%02x",
                                         packet.opcode());
        SendErrorResponse(packet.opcode(), 0, ErrorCode::kRequestNotSupported);
        break;
    }
  });
}

}  // namespace att
}  // namespace bluetooth
