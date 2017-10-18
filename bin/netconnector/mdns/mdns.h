// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "garnet/bin/netconnector/mdns/dns_message.h"
#include "garnet/bin/netconnector/mdns/mdns_agent.h"
#include "garnet/bin/netconnector/mdns/mdns_transceiver.h"
#include "garnet/bin/netconnector/mdns/resource_renewer.h"
#include "garnet/bin/netconnector/socket_address.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fxl/time/time_point.h"
#include "lib/netconnector/fidl/mdns.fidl.h"

namespace netconnector {
namespace mdns {

// Implements mDNS.
class Mdns : public MdnsAgent::Host {
 public:
  using ResolveHostNameCallback =
      std::function<void(const std::string& host_name,
                         const IpAddress& v4_address,
                         const IpAddress& v6_address)>;
  using ServiceInstanceCallback =
      std::function<void(const std::string& service,
                         const std::string& instance,
                         const SocketAddress& v4_address,
                         const SocketAddress& v6_address,
                         const std::vector<std::string>& text)>;

  Mdns();

  virtual ~Mdns() override;

  // Enables the specified interface and family. Should be called before calling
  // |Start|. If |EnableInterface| isn't called prior to |Start|, |Mdns| will
  // use all available interfaces. Otherwise it uses just the interfaces that
  // have been enabled.
  void EnableInterface(const std::string& name, sa_family_t family);

  // Determines whether message traffic will be logged.
  void SetVerbose(bool verbose);

  // Starts the transceiver. Returns true if successful.
  bool Start(const std::string& host_name);

  // Stops the transceiver.
  void Stop();

  // Resolves |host_name| to one or two |IpAddress|es.
  void ResolveHostName(const std::string& host_name,
                       fxl::TimePoint timeout,
                       const ResolveHostNameCallback& callback);

  // Starts publishing the indicated service instance. Returns false if and only
  // if the instance was already published.
  bool PublishServiceInstance(const std::string& service_name,
                              const std::string& instance_name,
                              IpPort port,
                              const std::vector<std::string>& text);

  // Stops publishing the indicated service instance.
  void UnpublishServiceInstance(const std::string& service_name,
                                const std::string& instance_name);

  // Registers interest in the specified service.
  std::shared_ptr<MdnsAgent> SubscribeToService(
      const std::string& service_name,
      const ServiceInstanceCallback& callback);

  // Adds a responder. Returns false if and only if the instance was already
  // published.
  bool AddResponder(const std::string& service_name,
                    const std::string& instance_name,
                    const std::vector<std::string>& announced_subtypes,
                    fidl::InterfaceHandle<MdnsResponder> responder);

 private:
  template <typename T>
  class reverse_priority_queue
      : public std::priority_queue<T, std::vector<T>, std::greater<T>> {};

  struct WakeQueueEntry {
    WakeQueueEntry(fxl::TimePoint time, std::shared_ptr<MdnsAgent> agent)
        : time_(time), agent_(agent) {}

    fxl::TimePoint time_;
    std::shared_ptr<MdnsAgent> agent_;

    bool operator>(const WakeQueueEntry& other) const {
      return time_ > other.time_;
    }
  };

  struct QuestionQueueEntry {
    QuestionQueueEntry(fxl::TimePoint time,
                       std::shared_ptr<DnsQuestion> question)
        : time_(time), question_(question) {}

    fxl::TimePoint time_;
    std::shared_ptr<DnsQuestion> question_;

    bool operator>(const QuestionQueueEntry& other) const {
      return time_ > other.time_;
    }
  };

  struct ResourceQueueEntry {
    ResourceQueueEntry(fxl::TimePoint time,
                       std::shared_ptr<DnsResource> resource,
                       MdnsResourceSection section)
        : time_(time), resource_(resource), section_(section) {}

    fxl::TimePoint time_;
    std::shared_ptr<DnsResource> resource_;
    MdnsResourceSection section_;

    bool operator>(const ResourceQueueEntry& other) const {
      return time_ > other.time_;
    }
  };

  // MdnsAgent::Host implementation.
  void WakeAt(std::shared_ptr<MdnsAgent> agent, fxl::TimePoint when) override;

  void SendQuestion(std::shared_ptr<DnsQuestion> question,
                    fxl::TimePoint when) override;

  void SendResource(std::shared_ptr<DnsResource> resource,
                    MdnsResourceSection section,
                    fxl::TimePoint when) override;

  void SendAddresses(MdnsResourceSection section, fxl::TimePoint when) override;

  void Renew(const DnsResource& resource) override;

  void RemoveAgent(MdnsAgent* agent,
                   const std::string& published_instance_full_name) override;

  // Misc private.
  void AddAgent(std::shared_ptr<MdnsAgent> agent);

  void SendMessage();

  void ReceiveQuestion(const DnsQuestion& question);

  void ReceiveResource(const DnsResource& resource,
                       MdnsResourceSection section);
  void PostTask();

  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  MdnsTransceiver transceiver_;
  std::string host_full_name_;
  bool started_ = false;
  reverse_priority_queue<fxl::TimePoint> post_task_queue_;
  reverse_priority_queue<WakeQueueEntry> wake_queue_;
  reverse_priority_queue<QuestionQueueEntry> question_queue_;
  reverse_priority_queue<ResourceQueueEntry> resource_queue_;
  std::unordered_map<MdnsAgent*, std::shared_ptr<MdnsAgent>> agents_;
  std::unordered_map<std::string, std::shared_ptr<MdnsAgent>>
      instance_publishers_by_instance_full_name_;
  std::shared_ptr<DnsResource> address_placeholder_;
  bool verbose_ = false;
  std::shared_ptr<ResourceRenewer> resource_renewer_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Mdns);
};

}  // namespace mdns
}  // namespace netconnector
