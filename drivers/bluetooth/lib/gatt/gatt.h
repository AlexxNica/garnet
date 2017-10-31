// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/drivers/bluetooth/lib/common/uuid.h"

namespace bluetooth {
namespace gatt {

// 16-bit Attribute Types defined by the GATT profile (Vol 3, Part G, 3.4).
constexpr uint16_t kPrimaryService = 0x2800;
constexpr uint16_t kSecondaryService = 0x2801;
constexpr uint16_t kInclude = 0x2802;
constexpr uint16_t kCharacteristic = 0x2803;
constexpr uint16_t kCharacteristicExtendedProperties = 0x2900;
constexpr uint16_t kCharacteristicUserDescription = 0x2901;
constexpr uint16_t kClientCharacteristicConfiguration = 0x2902;
constexpr uint16_t kServerCharacteristicConfiguration = 0x2903;
constexpr uint16_t kCharacteristicFormat = 0x2904;
constexpr uint16_t kCharacteristicAggregateFormat = 0x2905;

constexpr common::UUID kPrimaryServiceGroupType(kPrimaryService);
constexpr common::UUID kSecondaryServiceGroupType(kSecondaryService);
constexpr common::UUID kCharacteristicDeclarationType(kCharacteristic);
constexpr common::UUID kCEPType(kCharacteristicExtendedProperties);
constexpr common::UUID kCCCType(kClientCharacteristicConfiguration);
constexpr common::UUID kSCCType(kServerCharacteristicConfiguration);

// Possible values that can be used in a "Characteristic Properties" bitfield.
// (see Vol 3, Part G, 3.3.1.1)
constexpr uint8_t kCharacteristicPropertyBroadcast = 0x01;
constexpr uint8_t kCharacteristicPropertyRead = 0x02;
constexpr uint8_t kCharacteristicPropertyWriteWithoutResponse = 0x04;
constexpr uint8_t kCharacteristicPropertyWrite = 0x08;
constexpr uint8_t kCharacteristicPropertyNotify = 0x10;
constexpr uint8_t kCharacteristicPropertyIndicate = 0x20;
constexpr uint8_t kCharacteristicPropertyAuthenticatedSignedWrites = 0x40;
constexpr uint8_t kCharacteristicPropertyExtendedProperties = 0x80;

// Values for "Characteristic Extended Properties" bitfield.
// (see Vol 3, Part G, 3.3.3.1)
constexpr uint16_t kCharacteristicExtendedPropertyReliableWrite = 0x0001;
constexpr uint16_t kCharacteristicExtendedPropertyWritableAuxiliaries = 0x0002;

}  // namespace gatt
}  // namespace bluetooth
