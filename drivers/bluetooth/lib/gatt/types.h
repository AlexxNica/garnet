// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <vector>

#include "garnet/drivers/bluetooth/lib/att/attribute.h"
#include "garnet/drivers/bluetooth/lib/common/uuid.h"
#include "lib/fxl/macros.h"

namespace btlib {
namespace gatt {

// An identifier uniquely identifies a service, characteristic, or descriptor
// within an owning context. For example, in the server-role this ID may be
// unique within the scope of a local adapter. In the client-role, this could be
// scoped down to the peer on which this service exists.
using IdType = uint64_t;

class Characteristic;
using CharacteristicPtr = std::unique_ptr<Characteristic>;

class Service final {
 public:
  Service(bool primary, const common::UUID& type);
  ~Service() = default;

  bool primary() const { return primary_; }
  const common::UUID& type() const { return type_; }

  // The list of characteristics that have been added to this service.
  const std::vector<CharacteristicPtr>& characteristics() const {
    return characteristics_;
  }

  // Passes the ownership of this service's characteristics to the caller.
  std::vector<CharacteristicPtr> MoveCharacteristics() {
    return std::move(characteristics_);
  }

  // Adds the given characteristic to this service.
  inline void AddCharacteristic(CharacteristicPtr&& chr) {
    characteristics_.push_back(std::forward<CharacteristicPtr>(chr));
  }

 private:
  bool primary_;
  common::UUID type_;
  std::vector<CharacteristicPtr> characteristics_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Service);
};

using ServicePtr = std::unique_ptr<Service>;

class Descriptor;
using DescriptorPtr = std::unique_ptr<Descriptor>;

class Characteristic final {
 public:
  Characteristic(IdType id,
                 const common::UUID& type,
                 uint8_t properties,
                 uint16_t extended_properties,
                 const att::AccessRequirements& read_permissions,
                 const att::AccessRequirements& write_permissions);
  ~Characteristic() = default;

  IdType id() const { return id_; }
  const common::UUID& type() const { return type_; }
  uint8_t properties() const { return properties_; }
  uint16_t extended_properties() const { return extended_properties_; }

  const att::AccessRequirements& read_permissions() const {
    return read_permissions_;
  }

  const att::AccessRequirements& write_permissions() const {
    return write_permissions_;
  }

  const std::vector<DescriptorPtr>& descriptors() const { return descriptors_; }

  // Passes the ownership of this characteristic's descriptors to the caller.
  std::vector<DescriptorPtr> MoveDescriptors() {
    return std::move(descriptors_);
  }

  inline void AddDescriptor(DescriptorPtr&& desc) {
    descriptors_.push_back(std::forward<DescriptorPtr>(desc));
  }

 private:
  IdType id_;
  common::UUID type_;
  uint8_t properties_;
  uint16_t extended_properties_;
  att::AccessRequirements read_permissions_;
  att::AccessRequirements write_permissions_;
  std::vector<DescriptorPtr> descriptors_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Characteristic);
};

class Descriptor final {
 public:
  Descriptor(IdType id,
             const common::UUID& type,
             const att::AccessRequirements& read_permissions,
             const att::AccessRequirements& write_permissions);
  ~Descriptor() = default;

  IdType id() const { return id_; }
  const common::UUID& type() const { return type_; }

  const att::AccessRequirements& read_permissions() const {
    return read_permissions_;
  }

  const att::AccessRequirements& write_permissions() const {
    return write_permissions_;
  }

 private:
  IdType id_;
  common::UUID type_;
  att::AccessRequirements read_permissions_;
  att::AccessRequirements write_permissions_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Descriptor);
};

}  // namespace gatt
}  // namespace btlib
