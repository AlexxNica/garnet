// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddktl/device.h>
#include <ddktl/device-internal.h>
#include <driver/usb.h>
#include <magenta/listnode.h>
#include <mx/vmo.h>
#include <fbl/mutex.h>
#include <fbl/vector.h>

#include "drivers/audio/audio-proto/audio-proto.h"
#include "drivers/audio/dispatcher-pool/dispatcher-channel.h"

namespace audio {
namespace usb {

struct AudioStreamProtocol : public ddk::internal::base_protocol {
    explicit AudioStreamProtocol(bool is_input) {
        ddk_proto_id_ = is_input ? MX_PROTOCOL_AUDIO_INPUT : MX_PROTOCOL_AUDIO_OUTPUT;
    }

    bool is_input() const { return (ddk_proto_id_ == MX_PROTOCOL_AUDIO_INPUT); }
};

class UsbAudioStream;
using UsbAudioStreamBase = ddk::Device<UsbAudioStream,
                                       ddk::Ioctlable,
                                       ddk::Unbindable>;

class UsbAudioStream : public UsbAudioStreamBase,
                       public AudioStreamProtocol,
                       public DispatcherChannel::Owner {
public:
    static mx_status_t Create(bool is_input,
                              mx_device_t* parent,
                              usb_protocol_t* usb,
                              int index,
                              usb_interface_descriptor_t* usb_interface,
                              usb_endpoint_descriptor_t* usb_endpoint,
                              usb_audio_ac_format_type_i_desc* format_desc);

    void PrintDebugPrefix() const;

    // DDK device implementation
    void DdkUnbind();
    void DdkRelease();
    mx_status_t DdkIoctl(uint32_t op,
                         const void* in_buf, size_t in_len,
                         void* out_buf, size_t out_len, size_t* out_actual);


protected:
    // DispatcherChannel implementation
    mx_status_t ProcessChannel(DispatcherChannel* channel) override;
    void NotifyChannelDeactivated(const DispatcherChannel& channel) override;

private:
    friend class fbl::RefPtr<UsbAudioStream>;

    enum class RingBufferState {
        STOPPED,
        STOPPING,
        STOPPING_AFTER_UNPLUG,
        STARTING,
        STARTED,
    };

    UsbAudioStream(mx_device_t* parent, usb_protocol_t* usb, bool is_input, int usb_index)
        : UsbAudioStreamBase(parent),
          AudioStreamProtocol(is_input),
          usb_(*usb),
          usb_index_(usb_index),
          create_time_(mx_time_get(MX_CLOCK_MONOTONIC)) { }

    virtual ~UsbAudioStream();

    mx_status_t Bind(const char* devname,
                     usb_interface_descriptor_t* usb_interface,
                     usb_endpoint_descriptor_t* usb_endpoint,
                     usb_audio_ac_format_type_i_desc* format_desc);

    void ReleaseRingBufferLocked() __TA_REQUIRES(lock_);

    mx_status_t AddFormats(const usb_audio_ac_format_type_i_desc& format_desc,
                           fbl::Vector<audio_stream_format_range_t>* supported_formats);

    mx_status_t ProcessStreamChannelLocked(DispatcherChannel* channel) __TA_REQUIRES(lock_);
    mx_status_t ProcessRingBufChannelLocked(DispatcherChannel* channel) __TA_REQUIRES(lock_);

    // Stream command handlers
    mx_status_t OnGetStreamFormatsLocked(DispatcherChannel* channel,
                                         const audio_proto::StreamGetFmtsReq& req)
        __TA_REQUIRES(lock_);
    mx_status_t OnSetStreamFormatLocked(DispatcherChannel* channel,
                                        const audio_proto::StreamSetFmtReq& req)
        __TA_REQUIRES(lock_);
    mx_status_t OnGetGainLocked(DispatcherChannel* channel, const audio_proto::GetGainReq& req)
        __TA_REQUIRES(lock_);
    mx_status_t OnSetGainLocked(DispatcherChannel* channel, const audio_proto::SetGainReq& req)
        __TA_REQUIRES(lock_);
    mx_status_t OnPlugDetectLocked(DispatcherChannel* channel,
                                   const audio_proto::PlugDetectReq& req) __TA_REQUIRES(lock_);

    // Ring buffer command handlers
    mx_status_t OnGetFifoDepthLocked(DispatcherChannel* channel,
            const audio_proto::RingBufGetFifoDepthReq& req) __TA_REQUIRES(lock_);
    mx_status_t OnGetBufferLocked(DispatcherChannel* channel,
            const audio_proto::RingBufGetBufferReq& req) __TA_REQUIRES(lock_);
    mx_status_t OnStartLocked(DispatcherChannel* channel, const audio_proto::RingBufStartReq& req)
        __TA_REQUIRES(lock_);
    mx_status_t OnStopLocked(DispatcherChannel* channel, const audio_proto::RingBufStopReq& req)
        __TA_REQUIRES(lock_);

    void IotxnComplete(iotxn_t* txn);
    void QueueIotxnLocked() __TA_REQUIRES(txn_lock_);
    void CompleteIotxnLocked(iotxn_t* txn) __TA_REQUIRES(txn_lock_);

    usb_protocol_t                  usb_;
    fbl::Mutex lock_;
    fbl::Mutex txn_lock_ __TA_ACQUIRED_AFTER(lock_);

    fbl::RefPtr<DispatcherChannel> stream_channel_ __TA_GUARDED(lock_);
    fbl::RefPtr<DispatcherChannel> rb_channel_     __TA_GUARDED(lock_);

    // TODO(johngro) : support parsing and selecting from all of the format
    // descriptors present for a stream, not just a single format (with multiple
    // sample rates).
    fbl::Vector<audio_stream_format_range_t> supported_formats_;

    uint32_t                        frame_size_;
    uint32_t                        iso_packet_rate_;
    uint32_t                        bytes_per_packet_;
    uint32_t                        fifo_bytes_;
    uint32_t                        fractional_bpp_inc_;
    uint32_t                        fractional_bpp_acc_ __TA_GUARDED(txn_lock_);
    uint32_t                        ring_buffer_offset_ __TA_GUARDED(txn_lock_);
    uint64_t                        usb_frame_num_ __TA_GUARDED(txn_lock_);

    uint32_t                        bytes_per_notification_ = 0;
    uint32_t                        notification_acc_ __TA_GUARDED(txn_lock_);

    mx::vmo                         ring_buffer_vmo_;
    void*                           ring_buffer_virt_  = nullptr;
    uint32_t                        ring_buffer_size_  = 0;
    uint32_t                        ring_buffer_pos_ __TA_GUARDED(txn_lock_);
    volatile RingBufferState        ring_buffer_state_
        __TA_GUARDED(txn_lock_) = RingBufferState::STOPPED;

    union {
        audio_proto::RingBufStopResp  stop;
        audio_proto::RingBufStartResp start;
    } pending_job_resp_ __TA_GUARDED(txn_lock_);

    list_node_t                     free_iotxn_ __TA_GUARDED(txn_lock_);
    uint32_t                        free_iotxn_cnt_ __TA_GUARDED(txn_lock_);
    uint32_t                        allocated_iotxn_cnt_;
    uint32_t                        max_iotxn_size_;

    uint8_t                         iface_num_   = 0;
    uint8_t                         alt_setting_ = 0;
    uint8_t                         usb_ep_addr_ = 0;
    const int                       usb_index_;
    const mx_time_t                 create_time_;
    const uint64_t                  ticks_per_msec_ = mx_ticks_per_second() / 1000u;

    // TODO(johngro) : See MG-940.  eliminate this ASAP
    bool iotxn_complete_prio_bumped_ = false;
};

}  // namespace usb
}  // namespace audio
