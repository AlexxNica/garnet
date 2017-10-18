// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

#include "drivers/audio/intel-hda/codecs/utils/codec-driver-base.h"
#include "drivers/audio/intel-hda/codecs/utils/stream-base.h"
#include "drivers/audio/intel-hda/utils/audio2-proto.h"
#include "drivers/audio/intel-hda/utils/intel-hda-proto.h"

#include "debug-logging.h"

namespace audio {
namespace intel_hda {
namespace codecs {

mx_protocol_device_t IntelHDAStreamBase::STREAM_DEVICE_THUNKS = {
    .get_protocol = nullptr,
    .open         = nullptr,
    .openat       = nullptr,
    .close        = nullptr,
    .unbind       = nullptr,
    .release      = nullptr,
    .read         = nullptr,
    .write        = nullptr,
    .iotxn_queue  = nullptr,
    .get_size     = nullptr,
    .ioctl        = [](mx_device_t* stream_dev, uint32_t op,
                       const void* in_buf, size_t in_len,
                       void* out_buf, size_t out_len) -> ssize_t {
                        return reinterpret_cast<IntelHDAStreamBase*>(stream_dev->ctx)->
                            DeviceIoctl(op, in_buf, in_len, out_buf, out_len);
                    },
    .suspend      = nullptr,
    .resume       = nullptr,
};

IntelHDAStreamBase::IntelHDAStreamBase(uint32_t id, bool is_input)
    : id_(id),
      is_input_(is_input) {
    snprintf(dev_name_, sizeof(dev_name_), "%s-stream-%03u", is_input_ ? "input" : "output", id_);
    memset(&stream_device_, 0, sizeof(stream_device_));
}

void IntelHDAStreamBase::PrintDebugPrefix() const {
    printf("[%s] ", dev_name_);
}

mx_status_t IntelHDAStreamBase::Activate(const mxtl::RefPtr<DispatcherChannel>& codec_channel) {
    MX_DEBUG_ASSERT(codec_channel != nullptr);

    mxtl::AutoLock obj_lock(&obj_lock_);
    if (shutting_down_ || (codec_channel_ != nullptr))
        return ERR_BAD_STATE;

    // Remember our codec channel
    codec_channel_ = codec_channel;

    // Allow our implementation to send its initial stream setup commands to the
    // codec.
    mx_status_t res = OnActivateLocked();
    if (res != NO_ERROR)
        return res;

    // Request a DMA context
    ihda_proto::RequestStreamReq req;

    req.hdr.transaction_id = id();
    req.hdr.cmd = IHDA_CODEC_REQUEST_STREAM;
    req.input  = is_input();

    return codec_channel_->Write(&req, sizeof(req));
}

void IntelHDAStreamBase::Deactivate() {
    {
        mxtl::AutoLock obj_lock(&obj_lock_);
        DEBUG_LOG("Deactivating stream\n");
        shutting_down_ = true;  // prevent any new connections.

        // We should already have been removed from our codec's active stream list
        // at this point.
        MX_DEBUG_ASSERT(!this->InContainer());
    }


    // Disconnect from all of our clients.
    ShutdownDispatcherChannels();

    {
        mxtl::AutoLock obj_lock(&obj_lock_);
        MX_DEBUG_ASSERT(stream_channel_ == nullptr);

        // Allow our implementation to send the commands needed to tear down the
        // widgets which make up this stream.
        OnDeactivateLocked();

        // If we have been given a DMA stream by the IHDA controller, attempt to
        // return it now.
        if ((dma_stream_id_ != IHDA_INVALID_STREAM_ID) && (codec_channel_ != nullptr)) {
            ihda_proto::ReleaseStreamReq req;

            req.hdr.transaction_id = id();
            req.hdr.cmd = IHDA_CODEC_RELEASE_STREAM_NOACK,
            req.stream_id = dma_stream_id_;

            codec_channel_->Write(&req, sizeof(req));

            dma_stream_id_  = IHDA_INVALID_STREAM_ID;
            dma_stream_tag_ = IHDA_INVALID_STREAM_TAG;
        }

        // Let go of our reference to the codec device channel.
        codec_channel_ = nullptr;

        // If we had published a device node, remove it now.
        if (parent_device_ != nullptr) {
            device_remove(&stream_device_);
            parent_device_ = nullptr;
        }
    }

    DEBUG_LOG("Deactivate complete\n");
}

mx_status_t IntelHDAStreamBase::PublishDevice(mx_driver_t* codec_driver,
                                              mx_device_t* codec_device) {
    mxtl::AutoLock obj_lock(&obj_lock_);

    if ((codec_driver == nullptr) || (codec_device == nullptr)) return ERR_INVALID_ARGS;
    if (shutting_down_ || (parent_device_ != nullptr)) return ERR_BAD_STATE;

    // Initialize our device and fill out the protocol hooks
    device_init(&stream_device_, codec_driver, dev_name_, &STREAM_DEVICE_THUNKS);
    stream_device_.protocol_id  = is_input()
                                ? MX_PROTOCOL_AUDIO2_INPUT
                                : MX_PROTOCOL_AUDIO2_OUTPUT;
    stream_device_.protocol_ops = nullptr;
    stream_device_.ctx = this;

    // Publish the device.
    mx_status_t res = device_add(&stream_device_, codec_device);
    if (res != NO_ERROR) {
        LOG("Failed to add stream device for \"%s\" (res %d)\n", dev_name_, res);
        return res;
    }

    // Record our parent.
    parent_device_ = codec_device;

    return NO_ERROR;
}

mx_status_t IntelHDAStreamBase::ProcessSendCORBCmd(const ihda_proto::SendCORBCmdResp& resp) {
    return NO_ERROR;
}

mx_status_t IntelHDAStreamBase::ProcessRequestStream(const ihda_proto::RequestStreamResp& resp) {
    mxtl::AutoLock obj_lock(&obj_lock_);
    mx_status_t res;

    if (shutting_down_) return ERR_BAD_STATE;

    res = SetDMAStreamLocked(resp.stream_id, resp.stream_tag);
    if (res != NO_ERROR) {
        // TODO(johngro) : If we failed to set the DMA info because this stream
        // is in the process of shutting down, we really should return the
        // stream to the controller.
        //
        // Right now, we are going to return an error which will cause the lower
        // level infrastructure to close the codec device channel.  This will
        // prevent a leak (the core controller driver will re-claim the stream),
        // but it will also ruin all of the other streams in this codec are
        // going to end up being destroyed.  For simple codec driver who never
        // change stream topology, this is probably fine, but for more
        // complicated ones it probably is not.
        return res;
    }

    return OnDMAAssignedLocked();
}

mx_status_t IntelHDAStreamBase::ProcessSetStreamFmt(const ihda_proto::SetStreamFmtResp& codec_resp,
                                                    mx::channel&& ring_buffer_channel) {
    MX_DEBUG_ASSERT(ring_buffer_channel.is_valid());

    mxtl::AutoLock obj_lock(&obj_lock_);
    audio2_proto::StreamSetFmtResp resp;
    mx_status_t res = NO_ERROR;

    // Are we shutting down?
    if (shutting_down_) return ERR_BAD_STATE;

    // If we don't have a set format operation in flight, or the stream channel
    // has been closed, this set format operation has been canceled.  Do not
    // return an error up the stack; we don't want to close the connection to
    // our codec device.
    if ((set_format_tid_ == AUDIO2_INVALID_TRANSACTION_ID) ||
        (stream_channel_ == nullptr))
        goto finished;

    // Let the implementation send the commands required to finish changing the
    // stream format.
    res = FinishChangeStreamFormatLocked(encoded_fmt_);
    if (res != NO_ERROR) {
        DEBUG_LOG("Failed to finish set format (enc fmt 0x%04hx res %d)\n", encoded_fmt_, res);
        goto finished;
    }

    // Respond to the caller, transferring the DMA handle back in the process.
    resp.hdr.cmd = AUDIO2_STREAM_CMD_SET_FORMAT;
    resp.hdr.transaction_id = set_format_tid_;
    resp.result = NO_ERROR;
    res = stream_channel_->Write(&resp, sizeof(resp), mxtl::move(ring_buffer_channel));

finished:
    // Something went fatally wrong when trying to send the result back to the
    // caller.  Close the stream channel.
    if ((res != NO_ERROR) && (stream_channel_ != nullptr)) {
        stream_channel_->Deactivate(false);
        stream_channel_ = nullptr;
    }

    // One way or the other, this set format operation is finished.  Clear out
    // the in-flight transaction ID
    set_format_tid_ = AUDIO2_INVALID_TRANSACTION_ID;

    return NO_ERROR;
}

// TODO(johngro) : Refactor this; this sample_format of parameters is 95% the same
// between both the codec and stream base classes.
mx_status_t IntelHDAStreamBase::SendCodecCommandLocked(uint16_t  nid,
                                                       CodecVerb verb,
                                                       bool      no_ack) {
    if (codec_channel_ == nullptr) return ERR_BAD_STATE;

    ihda_codec_send_corb_cmd_req_t cmd;

    cmd.hdr.cmd = no_ack ? IHDA_CODEC_SEND_CORB_CMD_NOACK : IHDA_CODEC_SEND_CORB_CMD;
    cmd.hdr.transaction_id = id();
    cmd.nid = nid;
    cmd.verb = verb.val;

    return codec_channel_->Write(&cmd, sizeof(cmd));
}

mx_status_t IntelHDAStreamBase::SetDMAStreamLocked(uint16_t id, uint8_t tag) {
    if ((id == IHDA_INVALID_STREAM_ID) || (tag == IHDA_INVALID_STREAM_TAG))
        return ERR_INVALID_ARGS;

    MX_DEBUG_ASSERT((dma_stream_id_  == IHDA_INVALID_STREAM_ID) ==
                 (dma_stream_tag_ == IHDA_INVALID_STREAM_TAG));

    if (dma_stream_id_ != IHDA_INVALID_STREAM_ID)
        return ERR_BAD_STATE;

    dma_stream_id_  = id;
    dma_stream_tag_ = tag;

    return NO_ERROR;
}

ssize_t IntelHDAStreamBase::DeviceIoctl(uint32_t op,
                                        const void* in_buf,
                                        size_t in_len,
                                        void* out_buf,
                                        size_t out_len) {
    // The only IOCTL we support is get channel.
    if ((op != AUDIO2_IOCTL_GET_CHANNEL) ||
        (out_buf == nullptr)           ||
        (out_len != sizeof(mx_handle_t)))
        return ERR_INVALID_ARGS;

    // Enter the object lock and check to see if we are already bound to a
    // channel.  Currently, we do not support binding to multiple channels at
    // the same time.
    //
    // TODO(johngro) : Relax this restriction.  We want a single privileged
    // process to be allowed to bind to us and do things like set the stream
    // format and get access to the stream DMA channel.  OTOH, other processes
    // should be permitted to do things like query our supported formats,
    // perhaps change our volume settings, and so on.
    mxtl::AutoLock obj_lock(&obj_lock_);

    if (stream_channel_ != nullptr)
        return ERR_BAD_STATE;

    // Do not allow any new connections if we are in the process of shutting down
    if (shutting_down_)
        return ERR_BAD_STATE;

    // Attempt to allocate a new driver channel and bind it to us.
    auto channel = DispatcherChannelAllocator::New();
    if (channel == nullptr)
        return ERR_NO_MEMORY;

    mx::channel client_endpoint;
    mx_status_t res = channel->Activate(mxtl::WrapRefPtr(this), &client_endpoint);
    if (res == NO_ERROR) {
        stream_channel_ = channel;
        *(reinterpret_cast<mx_handle_t*>(out_buf)) = client_endpoint.release();
    }

    return res;
}

mx_status_t IntelHDAStreamBase::DoSetStreamFormatLocked(const audio2_proto::StreamSetFmtReq& fmt) {
    ihda_proto::SetStreamFmtReq req;
    uint16_t encoded_fmt;
    mx_status_t res;

    // If we don't have a DMA stream assigned to us, or there is already a set
    // format operation in flight, we cannot proceed.
    if ((dma_stream_id_  == IHDA_INVALID_STREAM_ID) ||
        (set_format_tid_ != AUDIO2_INVALID_TRANSACTION_ID)) {
        res = ERR_BAD_STATE;
        goto send_fail_response;
    }

    // If we cannot encode this stream format, then we definitely do not support it.
    res = EncodeStreamFormat(fmt, &encoded_fmt);
    if (res != NO_ERROR) {
        DEBUG_LOG("Failed to encode stream format %u:%hu:%s (res %d)\n",
                  fmt.frames_per_second,
                  fmt.channels,
                  audio2_proto::SampleFormatToString(fmt.sample_format),
                  res);
        goto send_fail_response;
    }

    // Let our implementation start the process of a format change.  This gives
    // it a chance to check the format for compatibility, and send commands to
    // quiesce the converters and amplifiers if it approves of the format.
    res = BeginChangeStreamFormatLocked(fmt);
    if (res != NO_ERROR) {
        DEBUG_LOG("Stream impl rejected stream format %u:%hu:%s (res %d)\n",
                  fmt.frames_per_second,
                  fmt.channels,
                  audio2_proto::SampleFormatToString(fmt.sample_format),
                  res);
        goto send_fail_response;
    }

    // Set the format of DMA stream.  This will stop any stream in progress and
    // close any connection to its clients.  At this point, all of our checks
    // are done and we expect success.  If anything goes wrong, consider it to
    // be a fatal internal error and close the connection to our client by
    // returning an error.
    MX_DEBUG_ASSERT(codec_channel_ != nullptr);
    req.hdr.cmd = IHDA_CODEC_SET_STREAM_FORMAT;
    req.hdr.transaction_id = id();
    req.stream_id = dma_stream_id_;
    req.format = encoded_fmt;
    res = codec_channel_->Write(&req, sizeof(req));
    if (res != NO_ERROR) {
        DEBUG_LOG("Failed to write set stream format %u:%hu:%s to codec channel (res %d)\n",
                  fmt.frames_per_second,
                  fmt.channels,
                  audio2_proto::SampleFormatToString(fmt.sample_format),
                  res);
        return res;
    }

    // Success!  Record the transaction ID of the request.  It indicates that the
    // format change is in progress, and will be needed to send the final
    // response back to the caller.
    set_format_tid_ = fmt.hdr.transaction_id;
    encoded_fmt_    = encoded_fmt;
    return NO_ERROR;

send_fail_response:
    audio2_proto::StreamSetFmtResp resp;
    resp.hdr = fmt.hdr;
    resp.result = res;

    MX_DEBUG_ASSERT(stream_channel_ != nullptr);

    res = stream_channel_->Write(&resp, sizeof(resp));
    if (res != NO_ERROR)
        DEBUG_LOG("Failing to write %zu bytes in response (res %d)\n", sizeof(resp), res);
    return res;
}

#define HANDLE_REQ(_ioctl, _payload, _handler)      \
case _ioctl:                                        \
    if (req_size != sizeof(req._payload)) {         \
        DEBUG_LOG("Bad " #_payload                  \
                  " reqonse length (%u != %zu)\n",  \
                  req_size, sizeof(req._payload));  \
        return ERR_INVALID_ARGS;                    \
    }                                               \
    return _handler(req._payload);
mx_status_t IntelHDAStreamBase::ProcessChannel(DispatcherChannel& channel,
                                               const mx_io_packet_t& io_packet) {
    mxtl::AutoLock obj_lock(&obj_lock_);

    // If our stream channel has already been closed, just get out early.  There
    // is not point in failing the request, the channel has already been
    // deactivated.
    if (stream_channel_ == nullptr)
        return NO_ERROR;

    // If we have lost our connection to the codec device, or are in the process
    // of shutting down, there is nothing further we can do.  Fail the request
    // and close the connection to the caller.
    if (shutting_down_ || (codec_channel_ == nullptr))
        return ERR_BAD_STATE;

    MX_DEBUG_ASSERT(&channel == stream_channel_.get());

    union {
        audio2_proto::CmdHdr          hdr;
        audio2_proto::StreamSetFmtReq set_format;
        // TODO(johngro) : add more commands here
    } req;

    static_assert(sizeof(req) <= 256,
                  "Request buffer is getting to be too large to hold on the stack!");

    uint32_t req_size;
    mx_status_t res = channel.Read(&req, sizeof(req), &req_size);
    if (res != NO_ERROR)
        return res;

    if ((req_size < sizeof(req.hdr) || (req.hdr.transaction_id == AUDIO2_INVALID_TRANSACTION_ID)))
        return ERR_INVALID_ARGS;

    switch (req.hdr.cmd) {
    HANDLE_REQ(AUDIO2_STREAM_CMD_SET_FORMAT, set_format, DoSetStreamFormatLocked);
    default:
        DEBUG_LOG("Unrecognized stream command 0x%04x\n", req.hdr.cmd);
        return ERR_NOT_SUPPORTED;
    }
}
#undef HANDLE_REQ

void IntelHDAStreamBase::NotifyChannelDeactivated(const DispatcherChannel& channel) {
    mxtl::AutoLock obj_lock(&obj_lock_);

    if (stream_channel_.get() != &channel)
        return;

    // Our user just closed their stream channel...  Should we stop any DMA
    // which is currently in progress, or is this OK?
    stream_channel_.reset();
}

// TODO(johngro) : Move this out to a utils library?
#define MAKE_RATE(_rate, _base, _mult, _div) \
    { .rate = _rate, .encoded = (_base << 14) | ((_mult - 1) << 11) | ((_div - 1) << 8) }
mx_status_t IntelHDAStreamBase::EncodeStreamFormat(const audio2_proto::StreamSetFmtReq& fmt,
                                                   uint16_t* encoded_fmt_out) {
    MX_DEBUG_ASSERT(encoded_fmt_out != nullptr);

    // See section 3.7.1
    // Start with the channel count.  Intel HDA DMA streams support between 1
    // and 16 channels.
    uint32_t channels = fmt.channels - 1;
    if ((fmt.channels < 1) || (fmt.channels > 16))
        return ERR_NOT_SUPPORTED;

    // Next determine the bit sample_format format
    uint32_t bits;
    switch (fmt.sample_format) {
    case AUDIO2_SAMPLE_FORMAT_8BIT:        bits = 0; break;
    case AUDIO2_SAMPLE_FORMAT_16BIT:       bits = 1; break;
    case AUDIO2_SAMPLE_FORMAT_20BIT_IN32:  bits = 2; break;
    case AUDIO2_SAMPLE_FORMAT_24BIT_IN32:  bits = 3; break;
    case AUDIO2_SAMPLE_FORMAT_32BIT:
    case AUDIO2_SAMPLE_FORMAT_32BIT_FLOAT: bits = 4; break;
    default: return ERR_NOT_SUPPORTED;
    }

    // Finally, determine the base frame rate, as well as the multiplier and
    // divisor.
    static const struct {
        uint32_t rate;
        uint32_t encoded;
    } RATE_ENCODINGS[] = {
        // 48 KHz family
        MAKE_RATE(  6000, 0, 1, 8),
        MAKE_RATE(  8000, 0, 1, 6),
        MAKE_RATE(  9600, 0, 1, 5),
        MAKE_RATE( 16000, 0, 1, 3),
        MAKE_RATE( 24000, 0, 1, 2),
        MAKE_RATE( 32000, 0, 2, 3),
        MAKE_RATE( 48000, 0, 1, 1),
        MAKE_RATE( 96000, 0, 2, 1),
        MAKE_RATE(144000, 0, 3, 1),
        MAKE_RATE(192000, 0, 4, 1),
        // 44.1 KHz family
        MAKE_RATE( 11025, 1, 1, 4),
        MAKE_RATE( 22050, 1, 1, 2),
        MAKE_RATE( 44100, 1, 1, 1),
        MAKE_RATE( 88200, 1, 2, 1),
        MAKE_RATE(176400, 1, 4, 1),
    };

    for (const auto& r : RATE_ENCODINGS) {
        if (r.rate == fmt.frames_per_second) {
            *encoded_fmt_out = static_cast<uint16_t>(r.encoded | channels | (bits << 4));
            return NO_ERROR;
        }
    }

    return ERR_NOT_SUPPORTED;
}
#undef MAKE_RATE

}  // namespace codecs
}  // namespace intel_hda
}  // namespace audio
