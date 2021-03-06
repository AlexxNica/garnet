// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl_helpers.h"

#include <unordered_set>

#include <endian.h>

#include "garnet/drivers/bluetooth/lib/common/uuid.h"
#include "garnet/drivers/bluetooth/lib/gap/advertising_data.h"
#include "garnet/drivers/bluetooth/lib/gap/discovery_filter.h"

// Make the FIDL namespace explicit.
namespace btfidl = ::bluetooth;

namespace bluetooth_service {
namespace fidl_helpers {
namespace {

::btfidl::control::TechnologyType TechnologyTypeToFidl(
    ::btlib::gap::TechnologyType type) {
  switch (type) {
    case ::btlib::gap::TechnologyType::kLowEnergy:
      return ::btfidl::control::TechnologyType::LOW_ENERGY;
    case ::btlib::gap::TechnologyType::kClassic:
      return ::btfidl::control::TechnologyType::CLASSIC;
    case ::btlib::gap::TechnologyType::kDualMode:
      return ::btfidl::control::TechnologyType::DUAL_MODE;
    default:
      FXL_NOTREACHED();
      break;
  }

  // This should never execute.
  return ::btfidl::control::TechnologyType::DUAL_MODE;
}

}  // namespace

::btfidl::StatusPtr NewErrorStatus(::bluetooth::ErrorCode error_code,
                                   const std::string& description) {
  auto status = ::btfidl::Status::New();
  status->error = ::btfidl::Error::New();
  status->error->error_code = error_code;
  status->error->description = description;

  return status;
}

::btfidl::control::AdapterInfoPtr NewAdapterInfo(
    const ::btlib::gap::Adapter& adapter) {
  auto adapter_info = ::btfidl::control::AdapterInfo::New();
  adapter_info->state = ::btfidl::control::AdapterState::New();

  // TODO(armansito): Most of these fields have not been implemented yet. Assign
  // the correct values when they are supported.
  adapter_info->state->powered = ::btfidl::Bool::New();
  adapter_info->state->powered->value = true;
  adapter_info->state->discovering = ::btfidl::Bool::New();
  adapter_info->state->discoverable = ::btfidl::Bool::New();

  adapter_info->identifier = adapter.identifier();
  adapter_info->address = adapter.state().controller_address().ToString();

  return adapter_info;
}

::btfidl::control::RemoteDevicePtr NewRemoteDevice(
    const ::btlib::gap::RemoteDevice& device) {
  auto fidl_device = ::btfidl::control::RemoteDevice::New();
  fidl_device->identifier = device.identifier();
  fidl_device->address = device.address().value().ToString();
  fidl_device->technology = TechnologyTypeToFidl(device.technology());

  // TODO(armansito): Report correct values once we support these.
  fidl_device->connected = false;
  fidl_device->bonded = false;

  // Set default value for device appearance.
  fidl_device->appearance = ::btfidl::control::Appearance::UNKNOWN;

  if (device.rssi() != ::btlib::hci::kRSSIInvalid) {
    fidl_device->rssi = ::btfidl::Int8::New();
    fidl_device->rssi->value = device.rssi();
  }

  ::btlib::gap::AdvertisingData adv_data;
  if (!::btlib::gap::AdvertisingData::FromBytes(device.advertising_data(),
                                                &adv_data))
    return nullptr;

  std::unordered_set<::btlib::common::UUID> uuids = adv_data.service_uuids();

  // |service_uuids| is not a nullable field, so we need to assign something to
  // it.
  if (uuids.empty()) {
    fidl_device->service_uuids.resize(0);
  } else {
    for (const auto& uuid : uuids) {
      fidl_device->service_uuids.push_back(uuid.ToString());
    }
  }

  if (adv_data.local_name())
    fidl_device->name = *adv_data.local_name();
  if (adv_data.appearance()) {
    fidl_device->appearance = static_cast<::btfidl::control::Appearance>(
        le16toh(*adv_data.appearance()));
  }
  if (adv_data.tx_power()) {
    auto fidl_tx_power = ::btfidl::Int8::New();
    fidl_tx_power->value = *adv_data.tx_power();
    fidl_device->tx_power = std::move(fidl_tx_power);
  }

  return fidl_device;
}

::btfidl::low_energy::RemoteDevicePtr NewLERemoteDevice(
    const ::btlib::gap::RemoteDevice& device) {
  ::btlib::gap::AdvertisingData ad;
  auto fidl_device = ::btfidl::low_energy::RemoteDevice::New();
  fidl_device->identifier = device.identifier();
  fidl_device->connectable = device.connectable();

  // Initialize advertising data only if its non-empty.
  if (device.advertising_data().size() != 0u) {
    ::btlib::gap::AdvertisingData ad;
    if (!::btlib::gap::AdvertisingData::FromBytes(device.advertising_data(),
                                                  &ad))
      return nullptr;

    fidl_device->advertising_data = ad.AsLEAdvertisingData();
  }

  if (device.rssi() != ::btlib::hci::kRSSIInvalid) {
    fidl_device->rssi = ::btfidl::Int8::New();
    fidl_device->rssi->value = device.rssi();
  }

  return fidl_device;
}

bool IsScanFilterValid(const ::btfidl::low_energy::ScanFilter& fidl_filter) {
  // |service_uuids| is the only field that can potentially contain invalid
  // data, since they are represented as strings.
  if (!fidl_filter.service_uuids)
    return true;

  for (const auto& uuid_str : fidl_filter.service_uuids) {
    if (!::btlib::common::IsStringValidUuid(uuid_str))
      return false;
  }

  return true;
}

bool PopulateDiscoveryFilter(
    const ::btfidl::low_energy::ScanFilter& fidl_filter,
    ::btlib::gap::DiscoveryFilter* out_filter) {
  FXL_DCHECK(out_filter);

  if (fidl_filter.service_uuids) {
    std::vector<::btlib::common::UUID> uuids;
    for (const auto& uuid_str : fidl_filter.service_uuids) {
      ::btlib::common::UUID uuid;
      if (!::btlib::common::StringToUuid(uuid_str, &uuid)) {
        FXL_VLOG(1) << "Invalid parameters given to scan filter";
        return false;
      }
      uuids.push_back(uuid);
    }

    if (!uuids.empty())
      out_filter->set_service_uuids(uuids);
  }

  if (fidl_filter.connectable) {
    out_filter->set_connectable(fidl_filter.connectable->value);
  }

  if (fidl_filter.manufacturer_identifier) {
    out_filter->set_manufacturer_code(
        fidl_filter.manufacturer_identifier->value);
  }

  if (fidl_filter.name_substring && !fidl_filter.name_substring.get().empty()) {
    out_filter->set_name_substring(fidl_filter.name_substring.get());
  }

  if (fidl_filter.max_path_loss) {
    out_filter->set_pathloss(fidl_filter.max_path_loss->value);
  }

  return true;
}

}  // namespace fidl_helpers
}  // namespace bluetooth_service
