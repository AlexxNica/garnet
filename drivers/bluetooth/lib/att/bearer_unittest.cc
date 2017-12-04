// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/att/bearer.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel_test.h"
#include "lib/fxl/macros.h"

namespace bluetooth {
namespace att {
namespace {

// Short timeout interval used in test cases that exercise transaction timeouts.
constexpr uint32_t kTestTimeoutMs = 50;

constexpr OpCode kTestRequest = kFindInformationRequest;
constexpr OpCode kTestResponse = kFindInformationResponse;
constexpr OpCode kTestRequest2 = kExchangeMTURequest;
constexpr OpCode kTestResponse2 = kExchangeMTUResponse;
constexpr OpCode kTestRequest3 = kFindByTypeValueRequest;
constexpr OpCode kTestResponse3 = kFindByTypeValueResponse;

constexpr OpCode kTestCommand = kWriteCommand;

void NopCallback(const PacketReader&) {}
void NopErrorCallback(bool, ErrorCode, Handle) {}
void NopHandler(Bearer::TransactionId, const PacketReader&) {}

class ATT_BearerTest : public l2cap::testing::FakeChannelTest {
 public:
  ATT_BearerTest() = default;
  ~ATT_BearerTest() override = default;

 protected:
  void SetUp() override {
    ChannelOptions options(l2cap::kATTChannelId);
    auto fake_chan = CreateFakeChannel(options);
    bearer_ = Bearer::Create(std::move(fake_chan));
  }

  void TearDown() override { bearer_ = nullptr; }

  Bearer* bearer() const { return bearer_.get(); }

  // Quits the test message loop if |callback| returns true. This is useful for
  // driving the message loop when a test expects multiple asynchronous
  // callbacks to get called and the invocation order of the callbacks is not
  // guaranteed.
  using CondFunc = std::function<bool()>;
  void QuitMessageLoopIf(const CondFunc& condition) {
    FXL_DCHECK(condition);
    if (condition())
      message_loop()->QuitNow();
  }

 private:
  fxl::RefPtr<Bearer> bearer_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ATT_BearerTest);
};

TEST_F(ATT_BearerTest, ShutDown) {
  ASSERT_TRUE(bearer()->is_open());

  // Verify that shutting down an open bearer notifies the closed callback.
  bool called = false;
  auto cb = [&called] { called = true; };

  bearer()->set_closed_callback(cb);
  bearer()->ShutDown();
  EXPECT_TRUE(called);
  EXPECT_FALSE(bearer()->is_open());

  // ShutDown() on a closed bearer does nothing.
  bearer()->ShutDown();
  EXPECT_FALSE(bearer()->is_open());
}

TEST_F(ATT_BearerTest, StartTransactionErrorClosed) {
  bearer()->ShutDown();
  ASSERT_FALSE(bearer()->is_open());

  EXPECT_FALSE(bearer()->StartTransaction(common::NewBuffer(kTestRequest),
                                          NopCallback, NopErrorCallback));
}

TEST_F(ATT_BearerTest, StartTransactionInvalidPacket) {
  // Empty
  EXPECT_FALSE(bearer()->StartTransaction(
      std::make_unique<common::BufferView>(), NopCallback, NopErrorCallback));

  // Exceeds MTU.
  bearer()->set_mtu(1);
  EXPECT_FALSE(bearer()->StartTransaction(common::NewBuffer(kTestRequest, 2),
                                          NopCallback, NopErrorCallback));
}

TEST_F(ATT_BearerTest, StartTransactionWrongMethodType) {
  // Command
  EXPECT_FALSE(bearer()->StartTransaction(common::NewBuffer(kWriteCommand),
                                          NopCallback, NopErrorCallback));

  // Notification
  EXPECT_FALSE(bearer()->StartTransaction(common::NewBuffer(kNotification),
                                          NopCallback, NopErrorCallback));
}

TEST_F(ATT_BearerTest, RequestTimeout) {
  bearer()->set_transaction_timeout_ms(kTestTimeoutMs);

  // We expect the channel to be closed and the pending transaction to end in an
  // error.
  bool closed = false;
  bool err_cb_called = false;
  auto cond = [&err_cb_called, &closed] { return err_cb_called && closed; };

  bearer()->set_closed_callback([&closed, cond, this] {
    closed = true;
    QuitMessageLoopIf(cond);
  });

  auto err_cb = [&err_cb_called, cond, this](bool timeout, ErrorCode code,
                                             Handle handle) {
    EXPECT_TRUE(timeout);
    EXPECT_EQ(ErrorCode::kNoError, code);
    EXPECT_EQ(0, handle);

    err_cb_called = true;
    QuitMessageLoopIf(cond);
  };

  EXPECT_TRUE(bearer()->StartTransaction(common::NewBuffer(kTestRequest),
                                         NopCallback, err_cb));

  RunMessageLoop();
  EXPECT_TRUE(closed);
  EXPECT_TRUE(err_cb_called);
}

// Queue many requests but make sure that FakeChannel only receives one.
TEST_F(ATT_BearerTest, RequestTimeoutMany) {
  bearer()->set_transaction_timeout_ms(kTestTimeoutMs);

  constexpr unsigned int kTransactionCount = 2;
  unsigned int chan_count = 0;
  auto chan_cb = [&chan_count](auto cb_packet) {
    chan_count++;
    // This should only be called once and for the first request.
    EXPECT_EQ(kTestRequest, (*cb_packet)[0]);
  };
  fake_chan()->SetSendCallback(chan_cb, message_loop()->task_runner());

  bool closed = false;
  unsigned int err_cb_count = 0u;
  auto cond = [&closed, &err_cb_count, kTransactionCount] {
    return closed && err_cb_count == kTransactionCount;
  };

  bearer()->set_closed_callback([&closed, cond, this] {
    closed = true;
    QuitMessageLoopIf(cond);
  });

  auto err_cb = [&err_cb_count, cond, this](bool timeout, ErrorCode code,
                                            Handle handle) {
    EXPECT_TRUE(timeout);
    EXPECT_EQ(ErrorCode::kNoError, code);
    EXPECT_EQ(0, handle);

    err_cb_count++;
    QuitMessageLoopIf(cond);
  };

  EXPECT_TRUE(bearer()->StartTransaction(
      common::NewBuffer(kTestRequest, 'T', 'e', 's', 't'), NopCallback,
      err_cb));
  EXPECT_TRUE(bearer()->StartTransaction(
      common::NewBuffer(kTestRequest2, 'T', 'e', 's', 't'), NopCallback,
      err_cb));

  RunMessageLoop();

  EXPECT_EQ(1u, chan_count);
  EXPECT_TRUE(closed);
  EXPECT_EQ(kTransactionCount, err_cb_count);
}

TEST_F(ATT_BearerTest, IndicationTimeout) {
  bearer()->set_transaction_timeout_ms(kTestTimeoutMs);

  // We expect the channel to be closed and the pending transaction to end in an
  // error.
  bool closed = false;
  bool err_cb_called = false;
  auto cond = [&err_cb_called, &closed] { return err_cb_called && closed; };

  bearer()->set_closed_callback([&closed, cond, this] {
    closed = true;
    QuitMessageLoopIf(cond);
  });

  auto err_cb = [&err_cb_called, cond, this](bool timeout, ErrorCode code,
                                             Handle handle) {
    EXPECT_TRUE(timeout);
    EXPECT_EQ(ErrorCode::kNoError, code);
    EXPECT_EQ(0, handle);

    err_cb_called = true;
    QuitMessageLoopIf(cond);
  };

  EXPECT_TRUE(bearer()->StartTransaction(
      common::NewBuffer(kIndication, 'T', 'e', 's', 't'), NopCallback, err_cb));

  RunMessageLoop();
  EXPECT_TRUE(closed);
  EXPECT_TRUE(err_cb_called);
}

// Queue many indications but make sure that FakeChannel only receives one.
TEST_F(ATT_BearerTest, IndicationTimeoutMany) {
  bearer()->set_transaction_timeout_ms(kTestTimeoutMs);

  constexpr unsigned int kTransactionCount = 2;
  constexpr uint8_t kIndValue1 = 1;
  constexpr uint8_t kIndValue2 = 2;

  unsigned int chan_count = 0;
  auto chan_cb = [kIndValue1, &chan_count](auto cb_packet) {
    chan_count++;
    // This should only be called once and for the first request.
    EXPECT_EQ(kIndValue1, (*cb_packet)[1]);
  };
  fake_chan()->SetSendCallback(chan_cb, message_loop()->task_runner());

  bool closed = false;
  unsigned int err_cb_count = 0u;
  auto cond = [&closed, &err_cb_count, kTransactionCount] {
    return closed && err_cb_count == kTransactionCount;
  };

  bearer()->set_closed_callback([&closed, cond, this] {
    closed = true;
    QuitMessageLoopIf(cond);
  });

  auto err_cb = [&err_cb_count, cond, this](bool timeout, ErrorCode code,
                                            Handle handle) {
    EXPECT_TRUE(timeout);
    EXPECT_EQ(ErrorCode::kNoError, code);
    EXPECT_EQ(0, handle);

    err_cb_count++;
    QuitMessageLoopIf(cond);
  };

  EXPECT_TRUE(bearer()->StartTransaction(
      common::NewBuffer(kIndication, kIndValue1), NopCallback, err_cb));
  EXPECT_TRUE(bearer()->StartTransaction(
      common::NewBuffer(kIndication, kIndValue2), NopCallback, err_cb));

  RunMessageLoop();

  EXPECT_EQ(1u, chan_count);
  EXPECT_TRUE(closed);
  EXPECT_EQ(kTransactionCount, err_cb_count);
}

TEST_F(ATT_BearerTest, ReceiveEmptyPacket) {
  bool closed = false;
  bearer()->set_closed_callback([&closed] {
    closed = true;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  });

  fake_chan()->Receive(common::BufferView());

  RunMessageLoop();
  EXPECT_TRUE(closed);
}

TEST_F(ATT_BearerTest, ReceiveResponseWithoutRequest) {
  bool closed = false;
  bearer()->set_closed_callback([&closed] {
    closed = true;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  });

  fake_chan()->Receive(common::CreateStaticByteBuffer(kTestResponse));

  RunMessageLoop();
  EXPECT_TRUE(closed);
}

TEST_F(ATT_BearerTest, ReceiveConfirmationWithoutIndication) {
  bool closed = false;
  bearer()->set_closed_callback([&closed] {
    closed = true;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  });

  fake_chan()->Receive(common::CreateStaticByteBuffer(kConfirmation));

  RunMessageLoop();
  EXPECT_TRUE(closed);
}

TEST_F(ATT_BearerTest, SendRequestWrongResponse) {
  unsigned int count = 0;
  auto chan_cb = [this, &count](auto cb_packet) {
    count++;
    // This should only be called once and for the first request.
    EXPECT_EQ(kTestRequest, (*cb_packet)[0]);

    // Send back the wrong response.
    fake_chan()->Receive(common::CreateStaticByteBuffer(kTestResponse2));
  };
  fake_chan()->SetSendCallback(chan_cb, message_loop()->task_runner());

  bool err_cb_called = false;
  bool closed = false;
  auto cond = [&err_cb_called, &closed] { return err_cb_called && closed; };

  bearer()->set_closed_callback([&closed, cond, this] {
    closed = true;
    QuitMessageLoopIf(cond);
  });

  auto err_cb = [&err_cb_called, cond, this](bool timeout, ErrorCode code,
                                             Handle handle) {
    EXPECT_FALSE(timeout);
    EXPECT_EQ(ErrorCode::kNoError, code);
    EXPECT_EQ(0, handle);

    err_cb_called = true;
    QuitMessageLoopIf(cond);
  };
  bearer()->StartTransaction(common::NewBuffer(kTestRequest), NopCallback,
                             err_cb);

  RunMessageLoop();
  EXPECT_TRUE(closed);
  EXPECT_TRUE(err_cb_called);
  EXPECT_EQ(1u, count);
}

TEST_F(ATT_BearerTest, SendRequestErrorResponseTooShort) {
  auto malformed_error_rsp = common::CreateStaticByteBuffer(
      // Opcode: error response
      kErrorResponse,

      // Parameters are too short (by 1 byte). Contents are unimportant, as the
      // PDU should be rejected.
      1, 2, 3);

  bool chan_cb_called = false;
  auto chan_cb = [this, &chan_cb_called, &malformed_error_rsp](auto cb_packet) {
    ASSERT_FALSE(chan_cb_called);
    EXPECT_EQ(kTestRequest, (*cb_packet)[0]);

    chan_cb_called = true;
    fake_chan()->Receive(malformed_error_rsp);
  };
  fake_chan()->SetSendCallback(chan_cb, message_loop()->task_runner());

  bool err_cb_called = false;
  bool closed = false;
  auto cond = [&err_cb_called, &closed] { return err_cb_called && closed; };

  bearer()->set_closed_callback([&closed, cond, this] {
    closed = true;
    QuitMessageLoopIf(cond);
  });

  auto err_cb = [&err_cb_called, cond, this](bool timeout, ErrorCode code,
                                             Handle handle) {
    EXPECT_FALSE(timeout);
    EXPECT_EQ(ErrorCode::kNoError, code);
    EXPECT_EQ(0, handle);

    err_cb_called = true;
    QuitMessageLoopIf(cond);
  };
  bearer()->StartTransaction(common::NewBuffer(kTestRequest), NopCallback,
                             err_cb);

  RunMessageLoop();
  EXPECT_TRUE(closed);
  EXPECT_TRUE(err_cb_called);
  EXPECT_TRUE(chan_cb_called);
}

TEST_F(ATT_BearerTest, SendRequestErrorResponseTooLong) {
  auto malformed_error_rsp = common::CreateStaticByteBuffer(
      // Opcode: error response
      kErrorResponse,

      // Parameters are too long (by 1 byte). Contents are unimportant, as the
      // PDU should be rejected.
      1, 2, 3, 4, 5);

  bool chan_cb_called = false;
  auto chan_cb = [this, &chan_cb_called, &malformed_error_rsp](auto cb_packet) {
    ASSERT_FALSE(chan_cb_called);
    EXPECT_EQ(kTestRequest, (*cb_packet)[0]);

    chan_cb_called = true;
    fake_chan()->Receive(malformed_error_rsp);
  };
  fake_chan()->SetSendCallback(chan_cb, message_loop()->task_runner());

  bool err_cb_called = false;
  bool closed = false;
  auto cond = [&err_cb_called, &closed] { return err_cb_called && closed; };

  bearer()->set_closed_callback([&closed, cond, this] {
    closed = true;
    QuitMessageLoopIf(cond);
  });

  auto err_cb = [&err_cb_called, cond, this](bool timeout, ErrorCode code,
                                             Handle handle) {
    EXPECT_FALSE(timeout);
    EXPECT_EQ(ErrorCode::kNoError, code);
    EXPECT_EQ(0, handle);

    err_cb_called = true;
    QuitMessageLoopIf(cond);
  };
  bearer()->StartTransaction(common::NewBuffer(kTestRequest), NopCallback,
                             err_cb);

  RunMessageLoop();
  EXPECT_TRUE(closed);
  EXPECT_TRUE(err_cb_called);
  EXPECT_TRUE(chan_cb_called);
}

TEST_F(ATT_BearerTest, SendRequestErrorResponseWrongOpCode) {
  auto error_rsp = common::CreateStaticByteBuffer(
      // Opcode: error response
      kErrorResponse,

      // request opcode: non-matching opcode in error response
      kTestRequest2,

      // handle, should be ignored
      0x00, 0x00,

      // error code:
      ErrorCode::kRequestNotSupported);

  bool chan_cb_called = false;
  auto chan_cb = [this, &chan_cb_called, &error_rsp](auto cb_packet) {
    ASSERT_FALSE(chan_cb_called);
    EXPECT_EQ(kTestRequest, (*cb_packet)[0]);

    chan_cb_called = true;
    fake_chan()->Receive(error_rsp);
  };
  fake_chan()->SetSendCallback(chan_cb, message_loop()->task_runner());

  bool err_cb_called = false;
  bool closed = false;
  auto cond = [&err_cb_called, &closed] { return err_cb_called && closed; };

  bearer()->set_closed_callback([&closed, cond, this] {
    closed = true;
    QuitMessageLoopIf(cond);
  });

  auto err_cb = [&err_cb_called, cond, this](bool timeout, ErrorCode code,
                                             Handle handle) {
    EXPECT_FALSE(timeout);
    EXPECT_EQ(ErrorCode::kNoError, code);
    EXPECT_EQ(0, handle);

    err_cb_called = true;
    QuitMessageLoopIf(cond);
  };
  bearer()->StartTransaction(common::NewBuffer(kTestRequest), NopCallback,
                             err_cb);

  RunMessageLoop();
  EXPECT_TRUE(closed);
  EXPECT_TRUE(err_cb_called);
  EXPECT_TRUE(chan_cb_called);
}

TEST_F(ATT_BearerTest, SendRequestErrorResponse) {
  auto error_rsp = common::CreateStaticByteBuffer(
      // Opcode: error response
      kErrorResponse,

      // request opcode
      kTestRequest,

      // handle (0x0001)
      0x01, 0x00,

      // error code:
      ErrorCode::kRequestNotSupported);

  bool chan_cb_called = false;
  auto chan_cb = [this, &chan_cb_called, &error_rsp](auto cb_packet) {
    ASSERT_FALSE(chan_cb_called);
    EXPECT_EQ(kTestRequest, (*cb_packet)[0]);

    chan_cb_called = true;
    fake_chan()->Receive(error_rsp);
  };
  fake_chan()->SetSendCallback(chan_cb, message_loop()->task_runner());

  bool err_cb_called = false;
  auto err_cb = [&err_cb_called](bool timeout, ErrorCode code, Handle handle) {
    EXPECT_FALSE(timeout);
    EXPECT_EQ(ErrorCode::kRequestNotSupported, code);
    EXPECT_EQ(0x0001, handle);

    err_cb_called = true;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };
  bearer()->StartTransaction(common::NewBuffer(kTestRequest), NopCallback,
                             err_cb);

  RunMessageLoop();
  EXPECT_TRUE(err_cb_called);
  EXPECT_TRUE(chan_cb_called);

  // The channel should remain open
  EXPECT_TRUE(bearer()->is_open());
}

TEST_F(ATT_BearerTest, SendRequestSuccess) {
  auto response =
      common::CreateStaticByteBuffer(kTestResponse, 'T', 'e', 's', 't');

  bool chan_cb_called = false;
  auto chan_cb = [this, &chan_cb_called, &response](auto cb_packet) {
    ASSERT_FALSE(chan_cb_called);
    EXPECT_EQ(kTestRequest, (*cb_packet)[0]);

    chan_cb_called = true;
    fake_chan()->Receive(response);
  };
  fake_chan()->SetSendCallback(chan_cb, message_loop()->task_runner());

  bool cb_called = false;
  auto cb = [&cb_called, &response](const auto& rsp_packet) {
    ASSERT_FALSE(cb_called);

    cb_called = true;
    EXPECT_TRUE(common::ContainersEqual(response, rsp_packet.data()));
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };
  bearer()->StartTransaction(common::NewBuffer(kTestRequest), cb,
                             NopErrorCallback);

  RunMessageLoop();
  EXPECT_TRUE(chan_cb_called);
  EXPECT_TRUE(cb_called);

  // The channel should remain open
  EXPECT_TRUE(bearer()->is_open());
}

TEST_F(ATT_BearerTest, SendManyRequests) {
  auto response1 = common::CreateStaticByteBuffer(kTestResponse, 'f', 'o', 'o');
  auto response2 =
      common::CreateStaticByteBuffer(kErrorResponse,

                                     // request opcode
                                     kTestRequest2,

                                     // handle (0x0001)
                                     0x01, 0x00,

                                     // error code:
                                     ErrorCode::kRequestNotSupported);
  auto response3 =
      common::CreateStaticByteBuffer(kTestResponse3, 'b', 'a', 'r');

  auto chan_cb = [&, this](auto cb_packet) {
    OpCode opcode = (*cb_packet)[0];

    if (opcode == kTestRequest)
      fake_chan()->Receive(response1);
    else if (opcode == kTestRequest2)
      fake_chan()->Receive(response2);
    else if (opcode == kTestRequest3)
      fake_chan()->Receive(response3);
  };
  fake_chan()->SetSendCallback(chan_cb, message_loop()->task_runner());

  unsigned int success_count = 0u;
  unsigned int error_count = 0u;

  auto error_cb = [&success_count, &error_count](bool timeout, ErrorCode code,
                                                 Handle handle) {
    // This should only be called for the second request (the first request
    // should have already succeeded).
    EXPECT_EQ(1u, success_count);
    EXPECT_FALSE(timeout);
    EXPECT_EQ(ErrorCode::kRequestNotSupported, code);
    EXPECT_EQ(0x0001, handle);

    error_count++;
  };

  // We expect each callback to be called in the order that we send the
  // corresponding request.
  auto callback1 = [&success_count, &response1](const auto& rsp_packet) {
    EXPECT_EQ(0u, success_count);
    EXPECT_TRUE(common::ContainersEqual(response1, rsp_packet.data()));
    success_count++;
  };
  bearer()->StartTransaction(common::NewBuffer(kTestRequest), callback1,
                             error_cb);

  auto callback2 = [](const auto& rsp_packet) {
    ADD_FAILURE() << "Transaction should have ended in error!";
  };
  bearer()->StartTransaction(common::NewBuffer(kTestRequest2), callback2,
                             error_cb);

  auto callback3 = [&success_count, &response3](const auto& rsp_packet) {
    EXPECT_EQ(1u, success_count);
    EXPECT_TRUE(common::ContainersEqual(response3, rsp_packet.data()));
    success_count++;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };
  bearer()->StartTransaction(common::NewBuffer(kTestRequest3), callback3,
                             error_cb);

  RunMessageLoop();

  EXPECT_EQ(2u, success_count);
  EXPECT_EQ(1u, error_count);
  EXPECT_TRUE(bearer()->is_open());
}

// An indication transaction can only fail in a circumstance that would shut
// down the bearer (e.g. a transaction timeout or an empty PDU). Otherwise,
// Bearer will only complete an indication transaction when it receives a
// confirmation PDU.
//
// NOTE: Bearer only looks at the opcode of a PDU and ignores the payload, so a
// malformed confirmation payload is not considered an error at this layer.
TEST_F(ATT_BearerTest, SendIndicationSuccess) {
  // Even though this is a malformed confirmation PDU it will not be rejected by
  // Bearer.
  auto conf = common::CreateStaticByteBuffer(kConfirmation, 'T', 'e', 's', 't');

  bool chan_cb_called = false;
  auto chan_cb = [this, &chan_cb_called, &conf](auto cb_packet) {
    ASSERT_FALSE(chan_cb_called);
    EXPECT_EQ(kIndication, (*cb_packet)[0]);

    chan_cb_called = true;
    fake_chan()->Receive(conf);
  };
  fake_chan()->SetSendCallback(chan_cb, message_loop()->task_runner());

  bool cb_called = false;
  auto cb = [&cb_called, &conf](const auto& packet) {
    ASSERT_FALSE(cb_called);

    cb_called = true;
    EXPECT_TRUE(common::ContainersEqual(conf, packet.data()));
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };
  bearer()->StartTransaction(common::NewBuffer(kIndication), cb,
                             NopErrorCallback);

  RunMessageLoop();
  EXPECT_TRUE(chan_cb_called);
  EXPECT_TRUE(cb_called);

  // The channel should remain open
  EXPECT_TRUE(bearer()->is_open());
}

TEST_F(ATT_BearerTest, SendWithoutResponseErrorClosed) {
  bearer()->ShutDown();
  ASSERT_FALSE(bearer()->is_open());

  EXPECT_FALSE(bearer()->SendWithoutResponse(common::NewBuffer(kTestCommand)));
}

TEST_F(ATT_BearerTest, SendWithoutResponseInvalidPacket) {
  // Empty
  EXPECT_FALSE(
      bearer()->SendWithoutResponse(std::make_unique<common::BufferView>()));

  // Exceeds MTU
  bearer()->set_mtu(1);
  EXPECT_FALSE(
      bearer()->SendWithoutResponse(common::NewBuffer(kTestCommand, 2)));
}

TEST_F(ATT_BearerTest, SendWithoutResponseWrongMethodType) {
  EXPECT_FALSE(bearer()->SendWithoutResponse(common::NewBuffer(kTestRequest)));
  EXPECT_FALSE(bearer()->SendWithoutResponse(common::NewBuffer(kTestResponse)));
  EXPECT_FALSE(bearer()->SendWithoutResponse(common::NewBuffer(kIndication)));
}

TEST_F(ATT_BearerTest, SendWithoutResponseCorrectMethodType) {
  EXPECT_TRUE(bearer()->SendWithoutResponse(common::NewBuffer(kNotification)));
  EXPECT_TRUE(bearer()->SendWithoutResponse(common::NewBuffer(kTestCommand)));
  EXPECT_TRUE(bearer()->SendWithoutResponse(
      common::NewBuffer(kTestRequest | kCommandFlag)));

  // Any opcode is accepted as long as it has the command flag set.
  EXPECT_TRUE(bearer()->SendWithoutResponse(
      common::NewBuffer(kInvalidOpCode | kCommandFlag)));
}

TEST_F(ATT_BearerTest, SendWithoutResponseMany) {
  // Everything should go through without any flow control.
  constexpr unsigned int kExpectedCount = 10;
  unsigned int chan_cb_count = 0u;

  auto chan_cb = [&chan_cb_count](auto cb_packet) {
    OpCode opcode = (*cb_packet)[0];
    EXPECT_TRUE(kCommandFlag & opcode || opcode == kIndication);

    chan_cb_count++;
    if (chan_cb_count == kExpectedCount)
      fsl::MessageLoop::GetCurrent()->QuitNow();
  };
  fake_chan()->SetSendCallback(chan_cb, message_loop()->task_runner());

  for (OpCode opcode = 0; opcode < kExpectedCount; opcode++) {
    // Everything
    EXPECT_TRUE(bearer()->SendWithoutResponse(
        common::NewBuffer(opcode | kCommandFlag)));
  }

  RunMessageLoop();
  EXPECT_EQ(kExpectedCount, chan_cb_count);
}

TEST_F(ATT_BearerTest, RegisterHandlerErrorClosed) {
  bearer()->ShutDown();
  EXPECT_FALSE(bearer()->is_open());
  EXPECT_EQ(Bearer::kInvalidHandlerId,
            bearer()->RegisterHandler(kWriteRequest, NopHandler));
  EXPECT_EQ(Bearer::kInvalidHandlerId,
            bearer()->RegisterHandler(kIndication, NopHandler));
}

TEST_F(ATT_BearerTest, RegisterHandlerErrorAlreadyRegistered) {
  EXPECT_NE(Bearer::kInvalidHandlerId,
            bearer()->RegisterHandler(kIndication, NopHandler));
  EXPECT_EQ(Bearer::kInvalidHandlerId,
            bearer()->RegisterHandler(kIndication, NopHandler));
}

TEST_F(ATT_BearerTest, UnregisterHandler) {
  auto id0 = bearer()->RegisterHandler(kNotification, NopHandler);
  EXPECT_NE(Bearer::kInvalidHandlerId, id0);

  bearer()->UnregisterHandler(id0);

  // It should be possible to register new handlers for the same opcodes.
  id0 = bearer()->RegisterHandler(kNotification, NopHandler);
  EXPECT_NE(Bearer::kInvalidHandlerId, id0);
}

TEST_F(ATT_BearerTest, RemoteTransactionNoHandler) {
  auto error_rsp = common::CreateStaticByteBuffer(
      // opcode
      kErrorResponse,

      // request opcode
      kTestRequest,

      // handle
      0x00, 0x00,

      // error code
      ErrorCode::kRequestNotSupported);

  bool received_error_rsp = false;
  auto chan_cb = [&received_error_rsp, &error_rsp](auto packet) {
    received_error_rsp = true;
    EXPECT_TRUE(common::ContainersEqual(error_rsp, *packet));
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };
  fake_chan()->SetSendCallback(chan_cb, message_loop()->task_runner());
  fake_chan()->Receive(common::CreateStaticByteBuffer(kTestRequest));

  RunMessageLoop();
  EXPECT_TRUE(received_error_rsp);
}

TEST_F(ATT_BearerTest, RemoteTransactionSeqProtocolError) {
  int request_count = 0;
  auto handler = [&request_count](auto id, const PacketReader& packet) {
    EXPECT_EQ(kTestRequest, packet.opcode());
    EXPECT_EQ(0u, packet.payload_size());

    request_count++;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  bearer()->RegisterHandler(kTestRequest, handler);
  fake_chan()->Receive(common::CreateStaticByteBuffer(kTestRequest));

  RunMessageLoop();
  ASSERT_EQ(1, request_count);

  // Receiving a second request before sending a response should close the
  // bearer.
  bool closed = false;
  bearer()->set_closed_callback([&closed] {
    closed = true;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  });

  fake_chan()->Receive(common::CreateStaticByteBuffer(kTestRequest));

  RunMessageLoop();
  EXPECT_TRUE(closed);
  EXPECT_EQ(1, request_count);
  EXPECT_FALSE(bearer()->is_open());
}

TEST_F(ATT_BearerTest, RemoteIndicationSeqProtocolError) {
  int ind_count = 0;
  auto handler = [&ind_count](auto id, const PacketReader& packet) {
    EXPECT_EQ(kIndication, packet.opcode());
    EXPECT_EQ(0u, packet.payload_size());

    ind_count++;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  bearer()->RegisterHandler(kIndication, handler);
  fake_chan()->Receive(common::CreateStaticByteBuffer(kIndication));

  RunMessageLoop();
  ASSERT_EQ(1, ind_count);

  // Receiving a second indication before sending a confirmation should close
  // the bearer.
  bool closed = false;
  bearer()->set_closed_callback([&closed] {
    closed = true;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  });

  fake_chan()->Receive(common::CreateStaticByteBuffer(kIndication));

  RunMessageLoop();
  EXPECT_TRUE(closed);
  EXPECT_EQ(1, ind_count);
  EXPECT_FALSE(bearer()->is_open());
}

TEST_F(ATT_BearerTest, ReplyInvalidPacket) {
  // Empty
  EXPECT_FALSE(bearer()->Reply(0, std::make_unique<common::BufferView>()));

  // Exceeds MTU.
  bearer()->set_mtu(1);
  EXPECT_FALSE(bearer()->Reply(0, common::NewBuffer(kTestRequest, 2)));
}

TEST_F(ATT_BearerTest, ReplyInvalidId) {
  EXPECT_FALSE(bearer()->Reply(Bearer::kInvalidTransactionId,
                               common::NewBuffer(kTestResponse)));

  // The ID is valid but doesn't correspond to an active transaction.
  EXPECT_FALSE(bearer()->Reply(1u, common::NewBuffer(kTestResponse)));
}

TEST_F(ATT_BearerTest, ReplyWrongOpCode) {
  Bearer::TransactionId id;
  bool handler_called = false;
  auto handler = [&id, &handler_called](auto cb_id,
                                        const PacketReader& packet) {
    EXPECT_EQ(kTestRequest, packet.opcode());
    EXPECT_EQ(0u, packet.payload_size());

    handler_called = true;
    id = cb_id;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  bearer()->RegisterHandler(kTestRequest, handler);
  fake_chan()->Receive(common::CreateStaticByteBuffer(kTestRequest));

  RunMessageLoop();
  ASSERT_TRUE(handler_called);

  EXPECT_FALSE(bearer()->Reply(id, common::NewBuffer(kTestResponse2)));
}

TEST_F(ATT_BearerTest, ReplyToIndicationWrongOpCode) {
  Bearer::TransactionId id;
  bool handler_called = false;
  auto handler = [&id, &handler_called](auto cb_id,
                                        const PacketReader& packet) {
    EXPECT_EQ(kIndication, packet.opcode());
    EXPECT_EQ(0u, packet.payload_size());

    handler_called = true;
    id = cb_id;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  bearer()->RegisterHandler(kIndication, handler);
  fake_chan()->Receive(common::CreateStaticByteBuffer(kIndication));

  RunMessageLoop();
  ASSERT_TRUE(handler_called);

  EXPECT_FALSE(bearer()->Reply(id, common::NewBuffer(kTestResponse)));
}

TEST_F(ATT_BearerTest, ReplyWithResponse) {
  bool response_sent = false;
  auto chan_cb = [&response_sent](auto packet) {
    response_sent = true;

    EXPECT_EQ(kTestResponse, (*packet)[0]);
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };
  fake_chan()->SetSendCallback(chan_cb, message_loop()->task_runner());

  Bearer::TransactionId id;
  bool handler_called = false;
  auto handler = [&id, &handler_called](auto cb_id,
                                        const PacketReader& packet) {
    EXPECT_EQ(kTestRequest, packet.opcode());
    EXPECT_EQ(0u, packet.payload_size());

    handler_called = true;
    id = cb_id;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  bearer()->RegisterHandler(kTestRequest, handler);
  fake_chan()->Receive(common::CreateStaticByteBuffer(kTestRequest));

  RunMessageLoop();
  ASSERT_TRUE(handler_called);

  EXPECT_TRUE(bearer()->Reply(id, common::NewBuffer(kTestResponse)));

  // The transaction is marked as complete.
  EXPECT_FALSE(bearer()->Reply(id, common::NewBuffer(kTestResponse)));
  EXPECT_FALSE(bearer()->ReplyWithError(id, 0, ErrorCode::kUnlikelyError));

  RunMessageLoop();
  EXPECT_TRUE(response_sent);
}

TEST_F(ATT_BearerTest, IndicationConfirmation) {
  bool conf_sent = false;
  auto chan_cb = [&conf_sent](auto packet) {
    conf_sent = true;
    EXPECT_EQ(kConfirmation, (*packet)[0]);
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };
  fake_chan()->SetSendCallback(chan_cb, message_loop()->task_runner());

  Bearer::TransactionId id;
  bool handler_called = false;
  auto handler = [&id, &handler_called](auto cb_id,
                                        const PacketReader& packet) {
    EXPECT_EQ(kIndication, packet.opcode());
    EXPECT_EQ(0u, packet.payload_size());

    handler_called = true;
    id = cb_id;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  bearer()->RegisterHandler(kIndication, handler);
  fake_chan()->Receive(common::CreateStaticByteBuffer(kIndication));

  RunMessageLoop();
  ASSERT_TRUE(handler_called);

  EXPECT_TRUE(bearer()->Reply(id, common::NewBuffer(kConfirmation)));

  // The transaction is marked as complete.
  EXPECT_FALSE(bearer()->Reply(id, common::NewBuffer(kConfirmation)));

  RunMessageLoop();
  EXPECT_TRUE(conf_sent);
}

TEST_F(ATT_BearerTest, ReplyWithErrorInvalidId) {
  EXPECT_FALSE(bearer()->ReplyWithError(0, 0, ErrorCode::kNoError));
}

TEST_F(ATT_BearerTest, IndicationReplyWithError) {
  Bearer::TransactionId id;
  bool handler_called = false;
  auto handler = [&id, &handler_called](auto cb_id,
                                        const PacketReader& packet) {
    EXPECT_EQ(kIndication, packet.opcode());
    EXPECT_EQ(0u, packet.payload_size());

    handler_called = true;
    id = cb_id;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  bearer()->RegisterHandler(kIndication, handler);
  fake_chan()->Receive(common::CreateStaticByteBuffer(kIndication));

  RunMessageLoop();
  ASSERT_TRUE(handler_called);

  // Cannot reply to an indication with error.
  EXPECT_FALSE(bearer()->ReplyWithError(id, 0, ErrorCode::kUnlikelyError));
}

TEST_F(ATT_BearerTest, ReplyWithError) {
  bool response_sent = false;
  auto chan_cb = [&response_sent](auto packet) {
    response_sent = true;

    // The error response that we send below
    auto expected = common::CreateStaticByteBuffer(
        kErrorResponse, kTestRequest, 0x00, 0x00, ErrorCode::kUnlikelyError);
    EXPECT_TRUE(common::ContainersEqual(expected, *packet));
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };
  fake_chan()->SetSendCallback(chan_cb, message_loop()->task_runner());

  Bearer::TransactionId id;
  bool handler_called = false;
  auto handler = [&id, &handler_called](auto cb_id,
                                        const PacketReader& packet) {
    EXPECT_EQ(kTestRequest, packet.opcode());
    EXPECT_EQ(0u, packet.payload_size());

    handler_called = true;
    id = cb_id;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };

  bearer()->RegisterHandler(kTestRequest, handler);
  fake_chan()->Receive(common::CreateStaticByteBuffer(kTestRequest));

  RunMessageLoop();
  ASSERT_TRUE(handler_called);

  EXPECT_TRUE(bearer()->ReplyWithError(id, 0, ErrorCode::kUnlikelyError));

  // The transaction is marked as complete.
  EXPECT_FALSE(bearer()->Reply(id, common::NewBuffer(kTestResponse)));
  EXPECT_FALSE(bearer()->ReplyWithError(id, 0, ErrorCode::kUnlikelyError));

  RunMessageLoop();
  EXPECT_TRUE(response_sent);
}

// Requests and indications have independent flow control
TEST_F(ATT_BearerTest, RequestAndIndication) {
  Bearer::TransactionId req_id, ind_id;

  int req_count = 0;
  int ind_count = 0;
  auto cond = [&req_count, &ind_count] {
    return req_count == 1 && ind_count == 1;
  };
  auto req_handler = [&req_id, &req_count, cond, this](auto id,
                                                       const auto& packet) {
    EXPECT_EQ(kTestRequest, packet.opcode());
    EXPECT_EQ(0u, packet.payload_size());

    req_count++;
    req_id = id;
    QuitMessageLoopIf(cond);
  };
  auto ind_handler = [&ind_id, &ind_count, cond, this](auto id,
                                                       const auto& packet) {
    EXPECT_EQ(kIndication, packet.opcode());
    EXPECT_EQ(0u, packet.payload_size());

    ind_count++;
    ind_id = id;
    QuitMessageLoopIf(cond);
  };

  bearer()->RegisterHandler(kTestRequest, req_handler);
  bearer()->RegisterHandler(kIndication, ind_handler);

  fake_chan()->Receive(common::CreateStaticByteBuffer(kTestRequest));
  fake_chan()->Receive(common::CreateStaticByteBuffer(kIndication));

  RunMessageLoop();
  EXPECT_EQ(1, req_count);
  ASSERT_EQ(1, ind_count);

  // Opcodes for the wrong transaction should be rejected.
  EXPECT_FALSE(bearer()->Reply(ind_id, common::NewBuffer(kTestResponse)));
  EXPECT_FALSE(bearer()->Reply(req_id, common::NewBuffer(kConfirmation)));

  // It should be possible to end two distinct transactions.
  EXPECT_TRUE(bearer()->Reply(req_id, common::NewBuffer(kTestResponse)));
  EXPECT_TRUE(bearer()->Reply(ind_id, common::NewBuffer(kConfirmation)));
}

// Test receipt of non-transactional PDUs.
TEST_F(ATT_BearerTest, RemotePDUWithoutResponse) {
  int cmd_count = 0;
  auto cmd_handler = [&cmd_count](auto tid, const auto& packet) {
    EXPECT_EQ(Bearer::kInvalidTransactionId, tid);
    EXPECT_EQ(kWriteCommand, packet.opcode());
    cmd_count++;
  };
  bearer()->RegisterHandler(kWriteCommand, cmd_handler);

  int not_count = 0;
  auto not_handler = [&not_count](auto tid, const auto& packet) {
    EXPECT_EQ(Bearer::kInvalidTransactionId, tid);
    EXPECT_EQ(kNotification, packet.opcode());
    not_count++;
    fsl::MessageLoop::GetCurrent()->QuitNow();
  };
  bearer()->RegisterHandler(kNotification, not_handler);

  fake_chan()->Receive(common::CreateStaticByteBuffer(kTestCommand));
  fake_chan()->Receive(common::CreateStaticByteBuffer(kNotification));

  RunMessageLoop();
  EXPECT_EQ(1, cmd_count);
  EXPECT_EQ(1, not_count);
}

}  // namespace
}  // namespace att
}  // namespace bluetooth
