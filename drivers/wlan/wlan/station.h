// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "device_interface.h"
#include "frame_handler.h"
#include "mac_frame.h"

#include "garnet/drivers/wlan/common/macaddr.h"
#include "garnet/drivers/wlan/common/moving_average.h"
#include "lib/wlan/fidl/wlan_mlme.fidl-common.h"
#include "lib/wlan/fidl/wlan_mlme_ext.fidl-common.h"

#include <ddk/protocol/wlan.h>
#include <fbl/unique_ptr.h>
#include <zircon/types.h>

namespace wlan {

class Packet;
class Timer;

class Station : public FrameHandler {
   public:
    Station(DeviceInterface* device, fbl::unique_ptr<Timer> timer);

    enum class PortState : bool { kBlocked = false, kOpen = true };

    enum class WlanState {
        // State 0
        kUnjoined,

        // State 1
        kUnauthenticated,

        // State 2
        kAuthenticated,
        // State 3/4
        // TODO(tkilbourn): distinguish between states where 802.1X ports are blocked
        kAssociated,
    };

    void Reset();

    const common::MacAddr* bssid() const {
        // TODO(porce): Distinguish cases
        // (1) if no Bss Descriptor came down from SME.
        // (2) if bssid_ is uninitlized.
        // (3) if bssid_ is kZeroMac.
        if (bss_.is_null()) { return nullptr; }
        return &bssid_;
    }

    uint16_t aid() const { return aid_; }

    wlan_channel_t channel() const {
        ZX_DEBUG_ASSERT(state_ != WlanState::kUnjoined);
        ZX_DEBUG_ASSERT(!bss_.is_null());
        return wlan_channel_t{bss_->channel};
    }

    zx_status_t SendKeepAliveResponse();

    bool ShouldDropMlmeMessage(Method& method) override;
    zx_status_t HandleMlmeJoinReq(const JoinRequest& req) override;
    zx_status_t HandleMlmeAuthReq(const AuthenticateRequest& req) override;
    zx_status_t HandleMlmeDeauthReq(const DeauthenticateRequest& req) override;
    zx_status_t HandleMlmeAssocReq(const AssociateRequest& req) override;
    zx_status_t HandleMlmeEapolReq(const EapolRequest& req) override;
    zx_status_t HandleMlmeSetKeysReq(const SetKeysRequest& req) override;

    bool ShouldDropDataFrame(const DataFrameHeader& hdr) override;
    zx_status_t HandleBeacon(const MgmtFrame<Beacon>& frame, const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleAuthentication(const MgmtFrame<Authentication>& frame,
                                     const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleDeauthentication(const MgmtFrame<Deauthentication>& frame,
                                       const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleAssociationResponse(const MgmtFrame<AssociationResponse>& frame,
                                          const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleDisassociation(const MgmtFrame<Disassociation>& frame,
                                     const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleAddBaRequestFrame(const MgmtFrame<AddBaRequestFrame>& frame,
                                        const wlan_rx_info_t& rxinfo) override;

    bool ShouldDropMgmtFrame(const MgmtFrameHeader& hdr) override;
    zx_status_t HandleNullDataFrame(const DataFrameHeader& frame,
                                    const wlan_rx_info_t& rxinfo) override;
    zx_status_t HandleDataFrame(const DataFrame<LlcHeader>& frame,
                                const wlan_rx_info_t& rxinfo) override;

    bool ShouldDropEthFrame(const BaseFrame<EthernetII>& hdr) override;
    zx_status_t HandleEthFrame(const BaseFrame<EthernetII>& frame) override;
    zx_status_t HandleTimeout();

    zx_status_t PreChannelChange(wlan_channel_t chan);
    zx_status_t PostChannelChange();

    const Timer& timer() const { return *timer_; }

   private:
    zx_status_t SendJoinResponse();
    zx_status_t SendAuthResponse(AuthenticateResultCodes code);
    zx_status_t SendDeauthResponse(const common::MacAddr& peer_sta_addr);
    zx_status_t SendDeauthIndication(uint16_t code);
    zx_status_t SendAssocResponse(AssociateResultCodes code);
    zx_status_t SendDisassociateIndication(uint16_t code);

    zx_status_t SendSignalReportIndication(uint8_t rssi);
    zx_status_t SendEapolResponse(EapolResultCodes result_code);
    zx_status_t SendEapolIndication(const EapolFrame* eapol, const common::MacAddr& src,
                                    const common::MacAddr& dst);

    zx_status_t SetPowerManagementMode(bool ps_mode);
    zx_status_t SendPsPoll();

    zx_time_t deadline_after_bcn_period(zx_duration_t tus);
    uint16_t next_seq();

    bool IsHTReady() const;
    HtCapabilities BuildHtCapabilities() const;

    DeviceInterface* device_;
    fbl::unique_ptr<Timer> timer_;
    BSSDescriptionPtr bss_;
    common::MacAddr bssid_;
    uint16_t last_seq_ = kMaxSequenceNumber;

    WlanState state_ = WlanState::kUnjoined;
    zx_time_t join_timeout_ = 0;
    zx_time_t auth_timeout_ = 0;
    zx_time_t assoc_timeout_ = 0;
    zx_time_t signal_report_timeout_ = 0;
    zx_time_t last_seen_ = 0;
    uint16_t aid_ = 0;
    common::MovingAverage<uint8_t, uint16_t, 20> avg_rssi_;
    AuthAlgorithm auth_alg_ = AuthAlgorithm::kOpenSystem;
    PortState controlled_port_ = PortState::kBlocked;
};

}  // namespace wlan
