// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/gatt/server.h"

#include "garnet/drivers/bluetooth/lib/att/database.h"
#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/gatt/gatt.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel_test.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace gatt {
namespace {

constexpr common::UUID kTestType16((uint16_t)0xBEEF);
constexpr common::UUID kTestType128({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
                                     13, 14, 15});

inline att::AccessRequirements AllowedNoSecurity() {
  return att::AccessRequirements(false, false, false);
}

class GATT_ServerTest : public l2cap::testing::FakeChannelTest {
 public:
  GATT_ServerTest() = default;
  ~GATT_ServerTest() override = default;

 protected:
  void SetUp() override {
    db_ = att::Database::Create();

    ChannelOptions options(l2cap::kATTChannelId);
    auto fake_chan = CreateFakeChannel(options);
    att_ = att::Bearer::Create(std::move(fake_chan));
    server_ = std::make_unique<Server>(db_, att_);
  }

  void TearDown() override {
    server_ = nullptr;
    att_ = nullptr;
    db_ = nullptr;
  }

  att::Database* db() const { return db_.get(); }
  att::Bearer* att() const { return att_.get(); }
  Server* server() const { return server_.get(); }

 private:
  fxl::RefPtr<att::Database> db_;
  fxl::RefPtr<att::Bearer> att_;
  std::unique_ptr<Server> server_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GATT_ServerTest);
};

TEST_F(GATT_ServerTest, ExchangeMTURequestInvalidPDU) {
  // Just opcode
  // clang-format off
  const auto kInvalidPDU = common::CreateStaticByteBuffer(0x02);
  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x02,        // request: exchange MTU
      0x00, 0x00,  // handle: 0
      0x04         // error: Invalid PDU
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kInvalidPDU, kExpected));
}

TEST_F(GATT_ServerTest, ExchangeMTURequestValueTooSmall) {
  constexpr uint16_t kServerMTU = l2cap::kDefaultMTU;
  constexpr uint16_t kClientMTU = 1;

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
    0x02,             // opcode: exchange MTU
    kClientMTU, 0x00  // client rx mtu: |kClientMTU|
  );

  const auto kExpected = common::CreateStaticByteBuffer(
    0x03,       // opcode: exchange MTU response
    0xA0, 0x02  // server rx mtu: |kServerMTU|
  );
  // clang-format on

  ASSERT_EQ(kServerMTU, att()->preferred_mtu());

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));

  // Should default to kLEMinMTU since kClientMTU is too small.
  EXPECT_EQ(att::kLEMinMTU, att()->mtu());
}

TEST_F(GATT_ServerTest, ExchangeMTURequest) {
  constexpr uint16_t kServerMTU = l2cap::kDefaultMTU;
  constexpr uint16_t kClientMTU = 0x64;

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
    0x02,             // opcode: exchange MTU
    kClientMTU, 0x00  // client rx mtu: |kClientMTU|
  );

  const auto kExpected = common::CreateStaticByteBuffer(
    0x03,       // opcode: exchange MTU response
    0xA0, 0x02  // server rx mtu: |kServerMTU|
  );
  // clang-format on

  ASSERT_EQ(kServerMTU, att()->preferred_mtu());

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));

  EXPECT_EQ(kClientMTU, att()->mtu());
}

TEST_F(GATT_ServerTest, FindInformationInvalidPDU) {
  // Just opcode
  // clang-format off
  const auto kInvalidPDU = common::CreateStaticByteBuffer(0x04);
  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x04,        // request: find information
      0x00, 0x00,  // handle: 0
      0x04         // error: Invalid PDU
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kInvalidPDU, kExpected));
}

TEST_F(GATT_ServerTest, FindInformationAttributeNotFound) {
  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x04,        // opcode: find information request
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF   // end: 0xFFFF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x04,        // request: find information
      0x01, 0x00,  // handle: 0x0001 (start handle in request)
      0x0A         // error: Attribute not found
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, FindInformation16) {
  const auto kTestValue = common::CreateStaticByteBuffer('t', 'e', 's', 't');

  auto* grp = db()->NewGrouping(kPrimaryServiceGroupType, 2, kTestValue);
  grp->AddAttribute(kTestType16, att::AccessRequirements(),
                    att::AccessRequirements());
  grp->AddAttribute(kTestType16, att::AccessRequirements(),
                    att::AccessRequirements());
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x04,        // opcode: find information request
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF   // end: 0xFFFF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x05,        // opcode: find information response
      0x01,        // format: 16-bit
      0x01, 0x00,  // handle: 0x0001
      0x00, 0x28,  // uuid: primary service group type
      0x02, 0x00,  // handle: 0x0002
      0xEF, 0xBE,  // uuid: 0xBEEF
      0x03, 0x00,  // handle: 0x0003
      0xEF, 0xBE   // uuid: 0xBEEF
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, FindInformation128) {
  const auto kTestValue = common::CreateStaticByteBuffer('t', 'e', 's', 't');

  auto* grp = db()->NewGrouping(kTestType128, 0, kTestValue);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x04,        // opcode: find information request
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF   // end: 0xFFFF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x05,        // opcode: find information response
      0x02,        // format: 128-bit
      0x01, 0x00,  // handle: 0x0001

      // uuid: 0F0E0D0C-0B0A-0908-0706-050403020100
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F);
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByGroupTypeInvalidPDU) {
  // Just opcode
  // clang-format off
  const auto kInvalidPDU = common::CreateStaticByteBuffer(0x10);
  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x10,        // request: read by group type
      0x00, 0x00,  // handle: 0
      0x04         // error: Invalid PDU
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kInvalidPDU, kExpected));
}

TEST_F(GATT_ServerTest, ReadByGroupTypeUnsupportedGroupType) {
  // 16-bit UUID
  // clang-format off
  const auto kUsing16BitType = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0x01, 0x00   // group type: 1 (unsupported)
  );

  // 128-bit UUID
  const auto kUsing128BitType = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF

      // group type: 00112233-4455-6677-8899-AABBCCDDEEFF (unsupported)
      0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x99, 0x88,
      0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00);

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x10,        // request: read by group type
      0x01, 0x00,  // handle: 0x0001 (start handle in request)
      0x10         // error: Unsupported Group Type
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kUsing16BitType, kExpected));
  EXPECT_TRUE(ReceiveAndExpect(kUsing128BitType, kExpected));
}

TEST_F(GATT_ServerTest, ReadByGroupTypeInvalidHandle) {
  // Start handle is 0
  // clang-format off
  const auto kInvalidStartHandle = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x00, 0x00,  // start: 0x0000
      0xFF, 0xFF,  // end: 0xFFFF
      0x00, 0x28   // group type: 0x2800 (primary service)
  );

  const auto kExpected1 = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x10,        // request: read by group type
      0x00, 0x00,  // handle: 0x0000 (start handle in request)
      0x01         // error: Invalid handle
  );

  // End handle is smaller than start handle
  const auto kInvalidEndHandle = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x02, 0x00,  // start: 0x0002
      0x01, 0x00,  // end: 0x0001
      0x00, 0x28   // group type: 0x2800 (primary service)
  );

  const auto kExpected2 = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x10,        // request: read by group type
      0x02, 0x00,  // handle: 0x0002 (start handle in request)
      0x01         // error: Invalid handle
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kInvalidStartHandle, kExpected1));
  EXPECT_TRUE(ReceiveAndExpect(kInvalidEndHandle, kExpected2));
}

TEST_F(GATT_ServerTest, ReadByGroupTypeAttributeNotFound) {
  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0x00, 0x28   // group type: 0x2800 (primary service)
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x10,        // request: read by group type
      0x01, 0x00,  // handle: 0x0001 (start handle in request)
      0x0A         // error: Attribute not found
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByGroupTypeSingle) {
  const auto kTestValue = common::CreateStaticByteBuffer('t', 'e', 's', 't');

  // Start: 1, end: 2
  auto* grp = db()->NewGrouping(kPrimaryServiceGroupType, 1, kTestValue);
  grp->AddAttribute(common::UUID(), att::AccessRequirements(),
                    att::AccessRequirements());
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0x00, 0x28   // group type: 0x2800 (primary service)
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x11,               // opcode: read by group type response
      0x08,               // length: 8 (strlen("test") + 4)
      0x01, 0x00,         // start: 0x0001
      0x02, 0x00,         // end: 0x0002
      't', 'e', 's', 't'  // value: "test"
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByGroupTypeSingle128) {
  const auto kTestValue = common::CreateStaticByteBuffer('t', 'e', 's', 't');

  // Start: 1, end: 2
  auto* grp = db()->NewGrouping(kPrimaryServiceGroupType, 1, kTestValue);
  grp->AddAttribute(common::UUID(), att::AccessRequirements(),
                    att::AccessRequirements());
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF

      // group type: 00002800-0000-1000-8000-00805F9B34FB (primary service)
      0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
      0x00, 0x10, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00);

  const auto kExpected = common::CreateStaticByteBuffer(
      0x11,               // opcode: read by group type response
      0x08,               // length: 8 (strlen("test") + 4)
      0x01, 0x00,         // start: 0x0001
      0x02, 0x00,         // end: 0x0002
      't', 'e', 's', 't'  // value: "test"
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByGroupTypeSingleTruncated) {
  const auto kTestValue = common::CreateStaticByteBuffer('t', 'e', 's', 't');

  // Start: 1, end: 1
  auto* grp = db()->NewGrouping(kPrimaryServiceGroupType, 0, kTestValue);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0x00, 0x28   // group type: 0x2800 (primary service)
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x11,        // opcode: read by group type response
      0x06,        // length: 6 (strlen("te") + 4)
      0x01, 0x00,  // start: 0x0001
      0x01, 0x00,  // end: 0x0001
      't', 'e'     // value: "te"
  );
  // clang-format on

  // Force the MTU to exactly fit |kExpected| which partially contains
  // |kTestValue|.
  att()->set_mtu(kExpected.size());

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByGroupTypeMultiple) {
  const auto kTestValue1 = common::CreateStaticByteBuffer('f', 'o', 'o');
  const auto kTestValue2 = common::CreateStaticByteBuffer('b', 'a', 'r');
  const auto kTestValue3 = common::CreateStaticByteBuffer('b', 'a', 'z');
  const auto kTestValue4 = common::CreateStaticByteBuffer('l', 'o', 'l');

  // Start: 1, end: 1
  auto* grp = db()->NewGrouping(kPrimaryServiceGroupType, 0, kTestValue1);
  grp->set_active(true);

  // Start: 2, end: 2
  grp = db()->NewGrouping(kPrimaryServiceGroupType, 0, kTestValue2);
  grp->set_active(true);

  // Start: 3, end: 3
  grp = db()->NewGrouping(kPrimaryServiceGroupType, 0, kTestValue3);
  grp->set_active(true);

  // Start: 4, end: 4
  grp = db()->NewGrouping(kPrimaryServiceGroupType, 0, kTestValue4);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x10,        // opcode: read by group type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0x00, 0x28   // group type: 0x2800 (primary service)
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x11,           // opcode: read by group type response
      0x07,           // length: 7 (strlen("foo") + 4)
      0x01, 0x00,     // start: 0x0001
      0x01, 0x00,     // end: 0x0001
      'f', 'o', 'o',  // value: "foo"
      0x02, 0x00,     // start: 0x0002
      0x02, 0x00,     // end: 0x0002
      'b', 'a', 'r',  // value: "bar"
      0x03, 0x00,     // start: 0x0003
      0x03, 0x00,     // end: 0x0003
      'b', 'a', 'z'   // value: "baz"
  );
  // clang-format on

  // Set the MTU to be one byte too short to include the 4th attribute group.
  att()->set_mtu(kExpected.size() + 6);

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeInvalidPDU) {
  // Just opcode
  // clang-format off
  const auto kInvalidPDU = common::CreateStaticByteBuffer(0x08);
  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x08,        // request: read by type
      0x00, 0x00,  // handle: 0
      0x04         // error: Invalid PDU
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kInvalidPDU, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeAttributeNotFound) {
  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x08,        // request: read by type
      0x01, 0x00,  // handle: 0x0001 (start handle in request)
      0x0A         // error: Attribute not found
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeDynamicValueNoHandler) {
  const auto kTestValue = common::CreateStaticByteBuffer('t', 'e', 's', 't');

  auto* grp = db()->NewGrouping(kPrimaryServiceGroupType, 1, kTestValue);
  grp->AddAttribute(kTestType16, AllowedNoSecurity(),
                    att::AccessRequirements());
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x08,        // request: read by type
      0x02, 0x00,  // handle: 0x0002 (the attribute causing the error)
      0x02         // error: Read not permitted
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeDynamicValue) {
  const auto kTestValue = common::CreateStaticByteBuffer('t', 'e', 's', 't');

  auto* grp = db()->NewGrouping(kPrimaryServiceGroupType, 1, kTestValue);
  auto* attr = grp->AddAttribute(kTestType16, AllowedNoSecurity(),
                                 att::AccessRequirements());
  attr->set_read_handler([](auto handle, uint16_t offset,
                            const auto& result_cb) {
    result_cb(att::ErrorCode::kNoError, common::CreateStaticByteBuffer('f', 'o', 'r', 'k'));
  });
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x09,               // opcode: read by type response
      0x06,               // length: 6 (strlen("fork") + 2)
      0x02, 0x00,         // handle: 0x0002
      'f', 'o', 'r', 'k'  // value: "fork"
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeDynamicValueError) {
  const auto kTestValue = common::CreateStaticByteBuffer('t', 'e', 's', 't');

  auto* grp = db()->NewGrouping(kPrimaryServiceGroupType, 1, kTestValue);
  auto* attr = grp->AddAttribute(kTestType16, AllowedNoSecurity(),
                                 att::AccessRequirements());
  attr->set_read_handler(
      [](auto handle, uint16_t offset, const auto& result_cb) {
        result_cb(att::ErrorCode::kUnlikelyError, common::BufferView());
      });
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x01,        // opcode: error response
      0x08,        // request: read by type
      0x02, 0x00,  // handle: 0x0002 (the attribute causing the error)
      0x0E         // error: Unlikely error 
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeSingle) {
  const auto kTestValue1 = common::CreateStaticByteBuffer('f', 'o', 'o');
  const auto kTestValue2 = common::CreateStaticByteBuffer('t', 'e', 's', 't');

  auto* grp = db()->NewGrouping(kPrimaryServiceGroupType, 1, kTestValue1);
  grp->AddAttribute(kTestType16, AllowedNoSecurity(), att::AccessRequirements())
      ->SetValue(kTestValue2);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x09,               // opcode: read by type response
      0x06,               // length: 6 (strlen("test") + 2)
      0x02, 0x00,         // handle: 0x0002
      't', 'e', 's', 't'  // value: "test"
  );

  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeSingle128) {
  const auto kTestValue1 = common::CreateStaticByteBuffer('f', 'o', 'o');
  const auto kTestValue2 = common::CreateStaticByteBuffer('t', 'e', 's', 't');

  auto* grp = db()->NewGrouping(kPrimaryServiceGroupType, 1, kTestValue1);
  grp->AddAttribute(kTestType128, AllowedNoSecurity(),
                    att::AccessRequirements())
      ->SetValue(kTestValue2);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF

      // type: 0F0E0D0C-0B0A-0908-0706-050403020100
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
      0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F);

  const auto kExpected = common::CreateStaticByteBuffer(
      0x09,               // opcode: read by type response
      0x06,               // length: 6 (strlen("test") + 2)
      0x02, 0x00,         // handle: 0x0002
      't', 'e', 's', 't'  // value: "test"
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeSingleTruncated) {
  const auto kTestValue1 = common::CreateStaticByteBuffer('f', 'o', 'o');
  const auto kTestValue2 = common::CreateStaticByteBuffer(
      't', 'e', 's', 't', 'i', 'n', 'g', ' ', 'i', 's', ' ', 'f', 'u', 'n');

  auto* grp = db()->NewGrouping(kPrimaryServiceGroupType, 1, kTestValue1);
  grp->AddAttribute(kTestType16, AllowedNoSecurity(), att::AccessRequirements())
      ->SetValue(kTestValue2);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x09,          // opcode: read by type response
      0x05,          // length: 5 (strlen("tes") + 2)
      0x02, 0x00,    // handle: 0x0002
      't', 'e', 's'  // value: "tes"
  );
  // clang-format on

  // Force the MTU to exactly fit |kExpected| which partially contains
  // |kTestValue2| (the packet is crafted so that both |kRequest| and
  // |kExpected| fit within the MTU).
  att()->set_mtu(kExpected.size());

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

TEST_F(GATT_ServerTest, ReadByTypeMultiple) {
  const auto kTestValue0 = common::CreateStaticByteBuffer('t', 'e', 's', 't');
  const auto kTestValue1 = common::CreateStaticByteBuffer('f', 'o', 'o');
  const auto kTestValue2 = common::CreateStaticByteBuffer('b', 'a', 'r');
  const auto kTestValue3 = common::CreateStaticByteBuffer('b', 'a', 'z');

  auto* grp = db()->NewGrouping(kPrimaryServiceGroupType, 3, kTestValue1);
  grp->AddAttribute(kTestType16, AllowedNoSecurity(), att::AccessRequirements())
      ->SetValue(kTestValue1);
  grp->AddAttribute(kTestType16, AllowedNoSecurity(), att::AccessRequirements())
      ->SetValue(kTestValue2);
  grp->AddAttribute(kTestType16, AllowedNoSecurity(), att::AccessRequirements())
      ->SetValue(kTestValue3);
  grp->set_active(true);

  // clang-format off
  const auto kRequest = common::CreateStaticByteBuffer(
      0x08,        // opcode: read by type
      0x01, 0x00,  // start: 0x0001
      0xFF, 0xFF,  // end: 0xFFFF
      0xEF, 0xBE   // type: 0xBEEF
  );

  const auto kExpected = common::CreateStaticByteBuffer(
      0x09,           // opcode: read by type response
      0x05,           // length: 5 (strlen("foo") + 2)
      0x02, 0x00,     // handle: 0x0002
      'f', 'o', 'o',  // value: "foo"
      0x03, 0x00,     // handle: 0x0003
      'b', 'a', 'r',  // value: "bar"
      0x04, 0x00,     // handle: 0x0004
      'b', 'a', 'z'   // value: "baz"
  );
  // clang-format on

  EXPECT_TRUE(ReceiveAndExpect(kRequest, kExpected));
}

}  // namespace
}  // namespace gatt
}  // namespace btlib
