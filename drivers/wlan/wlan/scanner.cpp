// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "scanner.h"

#include "device_interface.h"
#include "element.h"
#include "interface.h"
#include "logging.h"
#include "mac_frame.h"
#include "packet.h"
#include "serialize.h"
#include "timer.h"
#include "wlan.h"

#include "lib/fidl/cpp/bindings/array.h"

#include <zircon/assert.h>
#include <zx/time.h>

#include <cinttypes>
#include <utility>

namespace wlan {

Scanner::Scanner(DeviceInterface* device, fbl::unique_ptr<Timer> timer)
    : device_(device), timer_(std::move(timer)) {
    nbrs_bss_.Reset();
    ZX_DEBUG_ASSERT(timer_.get());
}

zx_status_t Scanner::Start(ScanRequestPtr req, ScanResponsePtr resp) {
    debugfn();
    resp_ = std::move(resp);
    resp_->bss_description_set = fidl::Array<BSSDescriptionPtr>::New(0);
    resp_->result_code = ScanResultCodes::NOT_SUPPORTED;

    if (IsRunning()) { return ZX_ERR_UNAVAILABLE; }
    ZX_DEBUG_ASSERT(req_.is_null());
    ZX_DEBUG_ASSERT(channel_index_ == 0);
    ZX_DEBUG_ASSERT(channel_start_ == 0);

    if (req->channel_list.size() == 0) { return ZX_ERR_INVALID_ARGS; }
    if (req->max_channel_time < req->min_channel_time) { return ZX_ERR_INVALID_ARGS; }
    if (!BSSTypes_IsValidValue(req->bss_type) || !ScanTypes_IsValidValue(req->scan_type)) {
        return ZX_ERR_INVALID_ARGS;
    }

    // TODO(tkilbourn): define another result code (out of spec) for errors that aren't
    // NOT_SUPPORTED errors. Then set SUCCESS only when we've successfully finished scanning.
    resp_->result_code = ScanResultCodes::SUCCESS;
    req_ = std::move(req);

    channel_start_ = timer_->Now();
    zx_time_t timeout;
    if (req_->scan_type == ScanTypes::PASSIVE) {
        timeout = channel_start_ + WLAN_TU(req_->min_channel_time);
    } else {
        timeout = channel_start_ + WLAN_TU(req_->probe_delay);
    }
    zx_status_t status = timer_->SetTimer(timeout);
    if (status != ZX_OK) { errorf("could not start scan timer: %d\n", status); }

    return status;
}

zx_status_t Scanner::Start(ScanRequestPtr req) {
    debugfn();
    if (IsRunning()) { return ZX_ERR_UNAVAILABLE; }
    ZX_DEBUG_ASSERT(req_.is_null());
    ZX_DEBUG_ASSERT(channel_index_ == 0);
    ZX_DEBUG_ASSERT(channel_start_ == 0);

    resp_ = ScanResponse::New();
    resp_->bss_description_set = fidl::Array<BSSDescriptionPtr>::New(0);
    resp_->result_code = ScanResultCodes::NOT_SUPPORTED;

    if (req->channel_list.size() == 0) { return SendScanResponse(); }
    if (req->max_channel_time < req->min_channel_time) { return SendScanResponse(); }
    if (!BSSTypes_IsValidValue(req->bss_type) || !ScanTypes_IsValidValue(req->scan_type)) {
        return SendScanResponse();
    }

    // TODO(tkilbourn): define another result code (out of spec) for errors that aren't
    // NOT_SUPPORTED errors. Then set SUCCESS only when we've successfully finished scanning.
    resp_->result_code = ScanResultCodes::SUCCESS;
    req_ = std::move(req);

    channel_start_ = timer_->Now();
    zx_time_t timeout = InitialTimeout();
    zx_status_t status = device_->SetChannel(ScanChannel());
    if (status != ZX_OK) {
        errorf("could not queue set channel: %d\n", status);
        SendScanResponse();
        Reset();
        return status;
    }

    status = timer_->SetTimer(timeout);
    if (status != ZX_OK) {
        errorf("could not start scan timer: %d\n", status);
        resp_->result_code = ScanResultCodes::NOT_SUPPORTED;
        SendScanResponse();
        Reset();
        return status;
    }

    return ZX_OK;
}

void Scanner::Reset() {
    debugfn();
    req_.reset();
    resp_.reset();
    channel_index_ = 0;
    channel_start_ = 0;
    timer_->CancelTimer();

    nbrs_bss_.Reset();
}

bool Scanner::IsRunning() const {
    return !req_.is_null();
}

Scanner::Type Scanner::ScanType() const {
    ZX_DEBUG_ASSERT(IsRunning());
    switch (req_->scan_type) {
    case ScanTypes::PASSIVE:
        return Type::kPassive;
    case ScanTypes::ACTIVE:
        return Type::kActive;
    }
}

wlan_channel_t Scanner::ScanChannel() const {
    debugfn();
    ZX_DEBUG_ASSERT(IsRunning());
    ZX_DEBUG_ASSERT(channel_index_ < req_->channel_list.size());

    return wlan_channel_t{req_->channel_list[channel_index_]};
}

// A ProbeResponse carries all currently used attributes of a Beacon frame. Hence, treat a
// ProbeResponse as a Beacon for now to support active scanning. There are additional information
// for either frame type which we have to process on a per frame type basis in the future. For now,
// stick with this kind of unification.
// TODO(hahnr): find a way to properly split up the Beacon and ProbeResponse processing
zx_status_t Scanner::HandleBeaconOrProbeResponse(const Packet* packet) {
    debugfn();
    ZX_DEBUG_ASSERT(IsRunning());

    auto rxinfo = packet->ctrl_data<wlan_rx_info_t>();
    ZX_DEBUG_ASSERT(rxinfo);

    auto hdr = packet->field<MgmtFrameHeader>(0);

    MacAddr bssid(hdr->addr3);
    MacAddr src_addr(hdr->addr2);

    if (bssid != src_addr) {
        // Undefined situation. Investigate if roaming needs this or this is a plain dark art.
        debugbcn("Rxed a beacon/probe_resp from the non-BSSID station: BSSID %s   SrcAddr %s\n",
                 MACSTR(bssid), MACSTR(src_addr));
        return ZX_OK;  // Do not process.
    }

    auto hdr_len = hdr->len();
    auto beacon = packet->field<Beacon>(hdr_len);
    size_t beacon_len = packet->len() - hdr_len;  // packet->len() does not include FCS.
    auto status = nbrs_bss_.Upsert(bssid, beacon, beacon_len, rxinfo);

    if (status != ZX_OK) {
        debugbcn("Failed to handle beacon (err %3d): BSSID %s timestamp: %15" PRIu64 "\n", status,
                 MACSTR(bssid), beacon->timestamp);
    }

    return ZX_OK;
}

zx_status_t Scanner::HandleTimeout() {
    debugfn();
    ZX_DEBUG_ASSERT(IsRunning());

    zx_time_t now = timer_->Now();
    zx_status_t status = ZX_OK;

    // Reached max channel dwell time
    if (now >= channel_start_ + WLAN_TU(req_->max_channel_time)) {
        debugf("reached max channel time\n");
        if (++channel_index_ >= req_->channel_list.size()) {
            timer_->CancelTimer();
            status = SendScanResponse();
            Reset();
            return status;
        } else {
            channel_start_ = timer_->Now();
            status = timer_->SetTimer(InitialTimeout());
            if (status != ZX_OK) { goto timer_fail; }
            return device_->SetChannel(ScanChannel());
        }
    }

    // TODO(tkilbourn): can probe delay come after min_channel_time?

    // Reached min channel dwell time
    if (now >= channel_start_ + WLAN_TU(req_->min_channel_time)) {
        debugf("Reached min channel time\n");
        // TODO(tkilbourn): if there was no sign of activity on this channel, skip ahead to the next
        // one
        // For now, just continue the scan.
        zx_time_t timeout = channel_start_ + WLAN_TU(req_->max_channel_time);
        status = timer_->SetTimer(timeout);
        if (status != ZX_OK) { goto timer_fail; }
        return ZX_OK;
    }

    // Reached probe delay for an active scan
    if (req_->scan_type == ScanTypes::ACTIVE &&
        now >= channel_start_ + WLAN_TU(req_->probe_delay)) {
        debugf("Reached probe delay\n");
        // TODO(hahnr): Add support for CCA as described in IEEE Std 802.11-2016 11.1.4.3.2 f)
        zx_time_t timeout = channel_start_ + WLAN_TU(req_->min_channel_time);
        status = timer_->SetTimer(timeout);
        if (status != ZX_OK) { goto timer_fail; }
        SendProbeRequest();
        return ZX_OK;
    }

    // Haven't reached a timeout yet; continue scanning
    return ZX_OK;

timer_fail:
    errorf("could not set scan timer: %d\n", status);
    status = SendScanResponse();
    Reset();
    return status;
}

zx_status_t Scanner::HandleError(zx_status_t error_code) {
    debugfn();
    resp_ = ScanResponse::New();
    // TODO(tkilbourn): report the error code somehow
    resp_->result_code = ScanResultCodes::NOT_SUPPORTED;
    return SendScanResponse();
}

zx_time_t Scanner::InitialTimeout() const {
    if (req_->scan_type == ScanTypes::PASSIVE) {
        return channel_start_ + WLAN_TU(req_->min_channel_time);
    } else {
        return channel_start_ + WLAN_TU(req_->probe_delay);
    }
}

// TODO(hahnr): support SSID list (IEEE Std 802.11-2016 11.1.4.3.2)
zx_status_t Scanner::SendProbeRequest() {
    debugfn();

    // TODO(hahnr): better size management; for now reserve 128 bytes for Probe elements
    size_t probe_len = sizeof(MgmtFrameHeader) + sizeof(ProbeRequest) + 128;
    fbl::unique_ptr<Buffer> buffer = GetBuffer(probe_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    const MacAddr& mymac = device_->GetState()->address();

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), probe_len));
    packet->clear();
    packet->set_peer(Packet::Peer::kWlan);
    auto hdr = packet->mut_field<MgmtFrameHeader>(0);
    hdr->fc.set_type(kManagement);
    hdr->fc.set_subtype(kProbeRequest);

    hdr->addr1 = kBcastMac;
    hdr->addr2 = mymac;
    hdr->addr3 = MacAddr(req_->bssid.data());

    // TODO(hahnr): keep reference to last sequence #?
    uint16_t seq = device_->GetState()->next_seq();
    hdr->sc.set_seq(seq);

    auto probe = packet->mut_field<ProbeRequest>(sizeof(MgmtFrameHeader));
    auto ele_len = packet->len() - sizeof(MgmtFrameHeader) - sizeof(ProbeRequest);
    ElementWriter w(probe->elements, ele_len);
    if (!w.write<SsidElement>(req_->ssid.data())) {
        errorf("could not write ssid \"%s\" to probe request\n", req_->ssid.data());
        return ZX_ERR_IO;
    }

    // TODO(hahnr): determine these rates based on hardware
    // Rates (in Mbps): 1, 2, 5.5, 6, 9, 11, 12, 18
    std::vector<uint8_t> rates = {0x02, 0x04, 0x0b, 0x0c, 0x12, 0x16, 0x18, 0x24};
    if (!w.write<SupportedRatesElement>(std::move(rates))) {
        errorf("could not write supported rates\n");
        return ZX_ERR_IO;
    }

    // Rates (in Mbps): 24, 36, 48, 54
    std::vector<uint8_t> ext_rates = {0x30, 0x48, 0x60, 0x6c};
    if (!w.write<ExtendedSupportedRatesElement>(std::move(ext_rates))) {
        errorf("could not write extended supported rates\n");
        return ZX_ERR_IO;
    }

    // Validate the request in debug mode
    ZX_DEBUG_ASSERT(probe->Validate(w.size()));

    size_t actual_len = sizeof(MgmtFrameHeader) + sizeof(ProbeRequest) + w.size();
    zx_status_t status = packet->set_len(actual_len);
    if (status != ZX_OK) {
        errorf("could not set packet length to %zu: %d\n", actual_len, status);
        return status;
    }

    status = device_->SendWlan(std::move(packet));
    if (status != ZX_OK) {
        errorf("could not send probe request packet: %d\n", status);
        return status;
    }
    return status;
}

zx_status_t Scanner::SendScanResponse() {
    debugfn();

    for (auto& iter : nbrs_bss_.map()) {
        Bss* bss = iter.second;
        if (bss == nullptr) continue;

        if (req_->ssid.size() == 0 || req_->ssid == bss->SsidToString()) {
            debugbss("%s\n", bss->ToString().c_str());
            resp_->bss_description_set.push_back(bss->ToFidl());
        }
    }

    size_t buf_len = sizeof(ServiceHeader) + resp_->GetSerializedSize();
    fbl::unique_ptr<Buffer> buffer = GetBuffer(buf_len);
    if (buffer == nullptr) { return ZX_ERR_NO_RESOURCES; }

    auto packet = fbl::unique_ptr<Packet>(new Packet(std::move(buffer), buf_len));
    packet->set_peer(Packet::Peer::kService);
    zx_status_t status = SerializeServiceMsg(packet.get(), Method::SCAN_confirm, resp_);
    if (status != ZX_OK) {
        errorf("could not serialize ScanResponse: %d\n", status);
    } else {
        status = device_->SendService(std::move(packet));
    }

    nbrs_bss_.Reset();  // TODO(porce): Decouple BSS management from Scanner.
    return status;
}

}  // namespace wlan
