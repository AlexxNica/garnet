// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gatt_server_fidl_impl.h"

#include "garnet/drivers/bluetooth/lib/common/uuid.h"
#include "garnet/drivers/bluetooth/lib/gatt/gatt.h"

#include "fidl_helpers.h"

// Make the FIDL namespace explicit.
namespace btfidl = ::bluetooth;

namespace bluetooth_service {
namespace {

::btlib::att::ErrorCode GattErrorCodeFromFidl(
    ::btfidl::gatt::ErrorCode error_code,
    bool is_read) {
  switch (error_code) {
    case ::btfidl::gatt::ErrorCode::NO_ERROR:
      return ::btlib::att::ErrorCode::kNoError;
    case ::btfidl::gatt::ErrorCode::INVALID_OFFSET:
      return ::btlib::att::ErrorCode::kInvalidOffset;
    case ::btfidl::gatt::ErrorCode::INVALID_VALUE_LENGTH:
      return ::btlib::att::ErrorCode::kInvalidAttributeValueLength;
    case ::btfidl::gatt::ErrorCode::NOT_PERMITTED:
      if (is_read)
        return ::btlib::att::ErrorCode::kReadNotPermitted;
      return ::btlib::att::ErrorCode::kWriteNotPermitted;
    default:
      break;
  }
  return ::btlib::att::ErrorCode::kUnlikelyError;
}

void ParseProperties(
    const ::fidl::Array<::btfidl::gatt::CharacteristicProperty>& properties,
    uint8_t* out_props,
    uint16_t* out_ext_props) {
  FXL_DCHECK(out_props);
  FXL_DCHECK(out_ext_props);

  *out_props = 0;
  *out_ext_props = 0;
  if (properties && !properties.empty()) {
    for (const auto& prop : properties) {
      switch (prop) {
        case ::btfidl::gatt::CharacteristicProperty::BROADCAST:
          *out_props |= ::btlib::gatt::kCharacteristicPropertyBroadcast;
          break;
        case ::btfidl::gatt::CharacteristicProperty::READ:
          *out_props |= ::btlib::gatt::kCharacteristicPropertyRead;
          break;
        case ::btfidl::gatt::CharacteristicProperty::WRITE_WITHOUT_RESPONSE:
          *out_props |=
              ::btlib::gatt::kCharacteristicPropertyWriteWithoutResponse;
          break;
        case ::btfidl::gatt::CharacteristicProperty::WRITE:
          *out_props |= ::btlib::gatt::kCharacteristicPropertyWrite;
          break;
        case ::btfidl::gatt::CharacteristicProperty::NOTIFY:
          *out_props |= ::btlib::gatt::kCharacteristicPropertyNotify;
          break;
        case ::btfidl::gatt::CharacteristicProperty::INDICATE:
          *out_props |= ::btlib::gatt::kCharacteristicPropertyIndicate;
          break;
        case ::btfidl::gatt::CharacteristicProperty::
            AUTHENTICATED_SIGNED_WRITES:
          *out_props |=
              ::btlib::gatt::kCharacteristicPropertyAuthenticatedSignedWrites;
          break;
        case ::btfidl::gatt::CharacteristicProperty::RELIABLE_WRITE:
          *out_props |=
              ::btlib::gatt::kCharacteristicPropertyExtendedProperties;
          *out_ext_props |=
              ::btlib::gatt::kCharacteristicExtendedPropertyReliableWrite;
          break;
        case ::btfidl::gatt::CharacteristicProperty::WRITABLE_AUXILIARIES:
          *out_props |=
              ::btlib::gatt::kCharacteristicPropertyExtendedProperties;
          *out_ext_props |=
              ::btlib::gatt::kCharacteristicExtendedPropertyWritableAuxiliaries;
          break;
      }
    }
  }
}

::btlib::att::AccessRequirements ParseSecurityRequirements(
    const ::btfidl::gatt::SecurityRequirementsPtr& reqs) {
  if (!reqs) {
    return ::btlib::att::AccessRequirements();
  }
  return ::btlib::att::AccessRequirements(reqs->encryption_required,
                                          reqs->authentication_required,
                                          reqs->authorization_required);
}

std::pair<::btlib::gatt::DescriptorPtr, std::string> NewDescriptor(
    const ::btfidl::gatt::Descriptor& fidl_desc) {
  auto read_reqs = ParseSecurityRequirements(fidl_desc.permissions->read);
  auto write_reqs = ParseSecurityRequirements(fidl_desc.permissions->write);

  ::btlib::common::UUID type;
  if (!::btlib::common::StringToUuid(fidl_desc.type.get(), &type)) {
    return std::make_pair<>(nullptr, "Invalid descriptor UUID");
  }

  auto desc = std::make_unique<::btlib::gatt::Descriptor>(
      fidl_desc.id, type, read_reqs, write_reqs);
  return std::make_pair<>(std::move(desc), "");
}

// Returns a characteristic and an error string. The error string will be empty
// on success. The characteristic will be null on error and the error string
// will contain a message.
std::pair<::btlib::gatt::CharacteristicPtr, std::string> NewCharacteristic(
    const ::btfidl::gatt::Characteristic& fidl_chrc) {
  uint8_t props;
  uint16_t ext_props;
  ParseProperties(fidl_chrc.properties, &props, &ext_props);

  if (!fidl_chrc.permissions) {
    return std::make_pair<>(nullptr, "Characteristic permissions missing");
  }

  auto read_reqs = ParseSecurityRequirements(fidl_chrc.permissions->read);
  auto write_reqs = ParseSecurityRequirements(fidl_chrc.permissions->write);

  ::btlib::common::UUID type;
  if (!::btlib::common::StringToUuid(fidl_chrc.type.get(), &type)) {
    return std::make_pair<>(nullptr, "Invalid characteristic UUID");
  }

  auto chrc = std::make_unique<::btlib::gatt::Characteristic>(
      fidl_chrc.id, type, props, ext_props, read_reqs, write_reqs);
  if (fidl_chrc.descriptors && !fidl_chrc.descriptors.empty()) {
    for (const auto& fidl_desc : fidl_chrc.descriptors) {
      if (!fidl_desc) {
        return std::make_pair<>(nullptr, "null descriptor");
      }
      auto desc_result = NewDescriptor(*fidl_desc);
      if (!desc_result.first) {
        return std::make_pair<>(nullptr, desc_result.second);
      }

      chrc->AddDescriptor(std::move(desc_result.first));
    }
  }

  return std::make_pair<>(std::move(chrc), "");
}

}  // namespace

GattServerFidlImpl::ServiceData::~ServiceData() {
  if (adapter) {
    adapter->le_connection_manager()->gatt_registry()->UnregisterService(id);
  }
}

GattServerFidlImpl::GattServerFidlImpl(
    AdapterManager* adapter_manager,
    ::fidl::InterfaceRequest<::btfidl::gatt::Server> request,
    const ConnectionErrorHandler& connection_error_handler)
    : adapter_manager_(adapter_manager),
      binding_(this, std::move(request)),
      weak_ptr_factory_(this) {
  FXL_DCHECK(adapter_manager_);
  FXL_DCHECK(connection_error_handler);
  adapter_manager_->AddObserver(this);
  binding_.set_connection_error_handler(
      [this, connection_error_handler] { connection_error_handler(this); });
}

GattServerFidlImpl::~GattServerFidlImpl() {
  adapter_manager_->RemoveObserver(this);

  // This will remove all of our services from their adapter.
  services_.clear();
}

void GattServerFidlImpl::PublishService(
    ::btfidl::gatt::ServicePtr fidl_service,
    ::fidl::InterfaceHandle<::btfidl::gatt::ServiceDelegate> delegate,
    const PublishServiceCallback& callback) {
  auto adapter = adapter_manager_->GetActiveAdapter();
  if (!adapter) {
    auto error = fidl_helpers::NewErrorStatus(
        ::btfidl::ErrorCode::BLUETOOTH_NOT_AVAILABLE,
        "Bluetooth not available on the current system");
    callback(std::move(error), 0u);
    return;
  }

  if (!fidl_service) {
    auto error = fidl_helpers::NewErrorStatus(
        ::btfidl::ErrorCode::INVALID_ARGUMENTS, "A service is required");
    callback(std::move(error), 0u);
    return;
  }

  if (!delegate) {
    auto error = fidl_helpers::NewErrorStatus(
        ::btfidl::ErrorCode::INVALID_ARGUMENTS, "A delegate is required");
    callback(std::move(error), 0u);
    return;
  }

  ::btlib::common::UUID service_type;
  if (!::btlib::common::StringToUuid(fidl_service->type.get(), &service_type)) {
    auto error = fidl_helpers::NewErrorStatus(
        ::btfidl::ErrorCode::INVALID_ARGUMENTS, "Invalid service UUID");
    callback(std::move(error), 0u);
    return;
  }

  // Process the FIDL service tree.
  auto service = std::make_unique<::btlib::gatt::Service>(fidl_service->primary,
                                                          service_type);
  if (fidl_service->characteristics && !fidl_service->characteristics.empty()) {
    for (const auto& fidl_chrc : fidl_service->characteristics) {
      if (!fidl_chrc) {
        auto error = fidl_helpers::NewErrorStatus(
            ::btfidl::ErrorCode::INVALID_ARGUMENTS, "null characteristic");
        callback(std::move(error), 0u);
        return;
      }

      auto chrc_result = NewCharacteristic(*fidl_chrc);
      if (!chrc_result.first) {
        auto error = fidl_helpers::NewErrorStatus(
            ::btfidl::ErrorCode::INVALID_ARGUMENTS, chrc_result.second);
        callback(std::move(error), 0u);
        return;
      }

      service->AddCharacteristic(std::move(chrc_result.first));
    }
  }

  auto self = weak_ptr_factory_.GetWeakPtr();
  auto read_handler = [self](auto svc_id, auto id, auto offset,
                             const auto& responder) {
    if (self) {
      self->OnReadRequest(svc_id, id, offset, responder);
    } else {
      responder(::btlib::att::ErrorCode::kUnlikelyError,
                ::btlib::common::BufferView());
    }
  };
  auto write_handler = [self](auto svc_id, auto id, auto offset,
                              const auto& value, const auto& responder) {
    if (self) {
      self->OnWriteRequest(svc_id, id, offset, value, responder);
    } else {
      responder(::btlib::att::ErrorCode::kUnlikelyError);
    }
  };

  auto id = adapter->le_connection_manager()->gatt_registry()->RegisterService(
      std::move(service), read_handler, write_handler);
  if (!id) {
    // TODO(armansito): Report a more detailed string if registration fails due
    // to duplicate ids.
    auto error = fidl_helpers::NewErrorStatus(::btfidl::ErrorCode::FAILED,
                                              "Failed to publish service");
    callback(std::move(error), 0u);
    return;
  }

  // TODO(armansito): IDs are unique per-adapter and not global, however,
  // since we unregister all services when an adapter changes, the IDs should
  // never clash.
  //
  // That said, we should consider making all services global and not tied to a
  // single adapter. The layering will make more sense once this FIDL impl is
  // provided by a specific bt-adapter device.
  FXL_DCHECK(services_.find(id) == services_.end());

  auto delegate_iface =
      ::btfidl::gatt::ServiceDelegatePtr::Create(std::move(delegate));

  // If a delegate disconnects, then we unregister the service that it
  // corresponds to.
  delegate_iface.set_connection_error_handler([self, id] {
    FXL_VLOG(1) << "GattServerFidlImpl: delegate disconnected; service removed";
    if (self)
      self->RemoveService(id);
  });

  ServiceData service_data;
  service_data.id = id;
  service_data.delegate = std::move(delegate_iface);
  service_data.adapter = adapter;
  services_[id] = std::move(service_data);

  callback(::btfidl::Status::New(), id);
}

void GattServerFidlImpl::RemoveService(uint64_t id) {
  auto iter = services_.find(id);
  if (iter == services_.end()) {
    FXL_VLOG(1) << "GattServerFidlImpl: service id not found: " << id;
    return;
  }

  services_.erase(iter);
}

void GattServerFidlImpl::OnActiveAdapterChanged(
    ::btlib::gap::Adapter* adapter) {
  // This will close all services and notify their connection error handlers.
  services_.clear();
}

void GattServerFidlImpl::OnReadRequest(
    ::btlib::gatt::IdType service_id,
    ::btlib::gatt::IdType id,
    uint16_t offset,
    const ::btlib::gatt::ReadResponder& responder) {
  auto iter = services_.find(service_id);
  if (iter == services_.end()) {
    responder(::btlib::att::ErrorCode::kUnlikelyError,
              ::btlib::common::BufferView());
    return;
  }

  auto cb = [responder](::fidl::Array<uint8_t> value, auto error_code) {
    responder(GattErrorCodeFromFidl(error_code, true /* is_read */),
              ::btlib::common::BufferView(value.data(), value.size()));
  };

  iter->second.delegate->OnReadValue(id, offset, cb);
}

void GattServerFidlImpl::OnWriteRequest(
    ::btlib::gatt::IdType service_id,
    ::btlib::gatt::IdType id,
    uint16_t offset,
    const ::btlib::common::ByteBuffer& value,
    const ::btlib::gatt::WriteResponder& responder) {
  auto iter = services_.find(service_id);
  if (iter == services_.end()) {
    responder(::btlib::att::ErrorCode::kUnlikelyError);
    return;
  }

  auto fidl_value = fidl::Array<uint8_t>::From(value);

  if (!responder) {
    iter->second.delegate->OnWriteWithoutResponse(id, offset,
                                                  std::move(fidl_value));
    return;
  }

  auto cb = [responder](auto error_code) {
    responder(GattErrorCodeFromFidl(error_code, false /* is_read */));
  };

  iter->second.delegate->OnWriteValue(id, offset, std::move(fidl_value), cb);
}

}  // namespace bluetooth_service
