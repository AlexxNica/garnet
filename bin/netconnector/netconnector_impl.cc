// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/netconnector/netconnector_impl.h"

#include <iostream>

#include "garnet/bin/netconnector/device_service_provider.h"
#include "garnet/bin/netconnector/host_name.h"
#include "garnet/bin/netconnector/mdns/mdns_names.h"
#include "garnet/bin/netconnector/netconnector_params.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

namespace netconnector {

// static
const IpPort NetConnectorImpl::kPort = IpPort::From_uint16_t(7777);
// static
const std::string NetConnectorImpl::kFuchsiaServiceName = "_fuchsia._tcp.";
// static
const std::string NetConnectorImpl::kLocalDeviceName = "local";

NetConnectorImpl::NetConnectorImpl(NetConnectorParams* params)
    : params_(params),
      application_context_(app::ApplicationContext::CreateFromStartupInfo()),
      // TODO(dalesat): Create a new RespondingServiceHost per user.
      // Requestors should provide user credentials allowing a ServiceAgent
      // to obtain a user environment. A RespondingServiceHost should be
      // created with that environment so that responding services are
      // launched in the correct environment.
      responding_service_host_(application_context_->environment()) {
  if (!params->listen()) {
    // Start the listener.
    NetConnectorPtr net_connector =
        application_context_->ConnectToEnvironmentService<NetConnector>();
    MdnsServicePtr mdns_service =
        application_context_->ConnectToEnvironmentService<MdnsService>();

    if (params_->mdns_verbose()) {
      mdns_service->SetVerbose(true);
    }

    if (params_->show_devices()) {
      net_connector->GetKnownDeviceNames(
          NetConnector::kInitialKnownDeviceNames,
          fxl::MakeCopyable([ this, net_connector = std::move(net_connector) ](
              uint64_t version, fidl::Array<fidl::String> device_names) {
            if (device_names.size() == 0) {
              std::cout << "No remote devices found\n";
            } else {
              for (auto& device_name : device_names) {
                std::cout << device_name << "\n";
              }
            }

            fsl::MessageLoop::GetCurrent()->PostQuitTask();
          }));
    } else {
      fsl::MessageLoop::GetCurrent()->PostQuitTask();
    }

    return;
  }

  // Running as the listener.

  device_names_publisher_.SetCallbackRunner(
      [this](const GetKnownDeviceNamesCallback& callback, uint64_t version) {
        fidl::Array<fidl::String> device_names =
            fidl::Array<fidl::String>::New(0);

        for (auto& pair : params_->devices()) {
          device_names.push_back(pair.first);
        }

        callback(version, std::move(device_names));
      });

  // Register services.
  for (auto& pair : params->MoveServices()) {
    responding_service_host_.RegisterSingleton(pair.first,
                                               std::move(pair.second));
  }

  listener_.Start(kPort, [this](fxl::UniqueFD fd) {
    AddServiceAgent(ServiceAgent::Create(std::move(fd), this));
  });

  application_context_->outgoing_services()->AddService<NetConnector>(
      [this](fidl::InterfaceRequest<NetConnector> request) {
        bindings_.AddBinding(this, std::move(request));
      });
  application_context_->outgoing_services()->AddService<MdnsService>(
      [this](fidl::InterfaceRequest<MdnsService> request) {
        mdns_service_impl_.AddBinding(std::move(request));
      });

  StartMdns();
}

NetConnectorImpl::~NetConnectorImpl() {}

void NetConnectorImpl::ReleaseDeviceServiceProvider(
    DeviceServiceProvider* device_service_provider) {
  size_t removed = device_service_providers_.erase(device_service_provider);
  FXL_DCHECK(removed == 1);
}

void NetConnectorImpl::ReleaseRequestorAgent(RequestorAgent* requestor_agent) {
  size_t removed = requestor_agents_.erase(requestor_agent);
  FXL_DCHECK(removed == 1);
}

void NetConnectorImpl::ReleaseServiceAgent(ServiceAgent* service_agent) {
  size_t removed = service_agents_.erase(service_agent);
  FXL_DCHECK(removed == 1);
}

void NetConnectorImpl::GetDeviceServiceProvider(
    const fidl::String& device_name,
    fidl::InterfaceRequest<app::ServiceProvider> request) {
  if (device_name == host_name_ || device_name == kLocalDeviceName) {
    responding_service_host_.AddBinding(std::move(request));
    return;
  }

  auto iter = params_->devices().find(device_name);
  if (iter == params_->devices().end()) {
    FXL_LOG(ERROR) << "Unrecognized device name " << device_name;
    return;
  }

  AddDeviceServiceProvider(DeviceServiceProvider::Create(
      device_name, SocketAddress(iter->second, kPort), std::move(request),
      this));
}

void NetConnectorImpl::GetKnownDeviceNames(
    uint64_t version_last_seen,
    const GetKnownDeviceNamesCallback& callback) {
  device_names_publisher_.Get(version_last_seen, callback);
}

void NetConnectorImpl::RegisterServiceProvider(
    const fidl::String& name,
    fidl::InterfaceHandle<app::ServiceProvider> handle) {
  FXL_LOG(INFO) << "Service '" << name << "' provider registered.";
  responding_service_host_.RegisterProvider(name, std::move(handle));
}

void NetConnectorImpl::AddDeviceServiceProvider(
    std::unique_ptr<DeviceServiceProvider> device_service_provider) {
  DeviceServiceProvider* raw_ptr = device_service_provider.get();
  device_service_providers_.emplace(raw_ptr,
                                    std::move(device_service_provider));
}

void NetConnectorImpl::AddRequestorAgent(
    std::unique_ptr<RequestorAgent> requestor_agent) {
  RequestorAgent* raw_ptr = requestor_agent.get();
  requestor_agents_.emplace(raw_ptr, std::move(requestor_agent));
}

void NetConnectorImpl::AddServiceAgent(
    std::unique_ptr<ServiceAgent> service_agent) {
  ServiceAgent* raw_ptr = service_agent.get();
  service_agents_.emplace(raw_ptr, std::move(service_agent));
}

void NetConnectorImpl::StartMdns() {
  // TODO(NET-79): Remove this check when NET-79 is fixed.
  if (!NetworkIsReady()) {
    fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [this]() { StartMdns(); }, fxl::TimeDelta::FromSeconds(5));
    return;
  }

  host_name_ = GetHostName();

  mdns_service_impl_.Start(host_name_, [this]() {
    mdns_service_impl_.PublishServiceInstance(
        kFuchsiaServiceName, mdns_service_impl_.host_name(), kPort,
        std::vector<std::string>());
  });

  mdns_service_impl_.SubscribeToService(
      kFuchsiaServiceName,
      [this](const std::string& service_name, const std::string& instance_name,
             const SocketAddress& v4_address, const SocketAddress& v6_address,
             const std::vector<std::string>& text) {
        if (v4_address.is_valid()) {
          FXL_LOG(INFO) << "Device '" << instance_name
                        << "' discovered at address " << v4_address.address();
          params_->RegisterDevice(instance_name, v4_address.address());
        } else if (v6_address.is_valid()) {
          FXL_LOG(INFO) << "Device '" << instance_name
                        << "' discovered at address " << v6_address.address();
          params_->RegisterDevice(instance_name, v6_address.address());
        } else {
          FXL_LOG(INFO) << "Device '" << instance_name << "' lost";
          params_->UnregisterDevice(instance_name);
        }

        device_names_publisher_.SendUpdates();
      });
}

}  // namespace netconnector
