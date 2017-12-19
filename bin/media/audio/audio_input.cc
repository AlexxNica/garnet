// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio/audio_input.h"

#include <audio-utils/audio-input.h>
#include <errno.h>
#include <fbl/auto_call.h>
#include <fbl/vector.h>
#include <fcntl.h>
#include <zircon/device/audio.h>

#include "garnet/bin/media/audio/driver_utils.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/media/timeline/timeline_rate.h"

namespace media {

constexpr uint32_t AudioInput::kPacketsPerRingBuffer;
constexpr uint32_t AudioInput::kPacketsPerSecond;

// static
std::shared_ptr<AudioInput> AudioInput::Create(const std::string& device_path) {
  std::shared_ptr<AudioInput> device(new AudioInput(device_path));

  zx_status_t result = device->Initalize();
  if (result != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to open and initialize audio input device \""
                   << device_path << "\" (res " << result << ")";
    return nullptr;
  }

  return device;
}

AudioInput::AudioInput(const std::string& device_path)
    : allocator_(PayloadAllocator::GetDefault()),
      state_(State::kUninitialized) {
  audio_input_ = audio::utils::AudioInput::Create(device_path.c_str());
}

zx_status_t AudioInput::Initalize() {
  if (state_ != State::kUninitialized) {
    return ZX_ERR_BAD_STATE;
  }

  if (audio_input_ == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }

  zx_status_t res = audio_input_->Open();
  if (res != ZX_OK) {
    return res;
  }

  fbl::Vector<audio_stream_format_range_t> formats;
  res = audio_input_->GetSupportedFormats(&formats);
  audio_input_->Close();
  if (res != ZX_OK) {
    return res;
  }

  for (const auto& fmt : formats) {
    driver_utils::AddAudioStreamTypeSets(fmt, &supported_stream_types_);
  }

  state_ = State::kStopped;

  return ZX_OK;
}

AudioInput::~AudioInput() {
  Stop();
}

std::vector<std::unique_ptr<media::StreamTypeSet>>
AudioInput::GetSupportedStreamTypes() {
  std::vector<std::unique_ptr<media::StreamTypeSet>> result;

  for (const auto& t : supported_stream_types_) {
    result.push_back(t->Clone());
  }

  return result;
}

bool AudioInput::SetStreamType(std::unique_ptr<StreamType> stream_type) {
  FXL_DCHECK(stream_type);
  if (state_ != State::kStopped) {
    FXL_LOG(ERROR) << "SetStreamType called after Start";
    return false;
  }

  // We are in the proper state to accept a SetStreamType request.  If the
  // request fails for any reason, the internal configuration should be
  // considered to be invalid.
  config_valid_ = false;
  bool compatible = false;
  for (const auto& t : supported_stream_types_) {
    if (t->Includes(*stream_type)) {
      compatible = true;
      break;
    }
  }

  if (!compatible) {
    FXL_LOG(ERROR) << "Unsupported stream type requested";
    return false;
  }

  // Convert the AudioStreamType::SampleFormat into an audio_sample_format_t
  // which the driver will understand.  This should really never fail.
  const auto& audio_stream_type = *stream_type->audio();
  if (!driver_utils::SampleFormatToDriverSampleFormat(
          audio_stream_type.sample_format(), &configured_sample_format_)) {
    FXL_LOG(ERROR) << "Failed to convert SampleFormat ("
                   << static_cast<uint32_t>(audio_stream_type.sample_format())
                   << ") to audio_sample_format_t";
    return false;
  }

  configured_frames_per_second_ = audio_stream_type.frames_per_second();
  configured_channels_ = audio_stream_type.channels();
  configured_bytes_per_frame_ = audio_stream_type.bytes_per_frame();
  config_valid_ = true;

  return true;
}

void AudioInput::Start() {
  FXL_DCHECK(state_ != State::kUninitialized);

  if (state_ != State::kStopped) {
    FXL_DCHECK(state_ == State::kStarted);
    return;
  }

  if (!config_valid_) {
    FXL_LOG(ERROR) << "Cannot start AudioInput.  "
                   << "Configuration is currently invalid.";
    return;
  }

  state_ = State::kStarted;
  worker_thread_ = std::thread([this]() { Worker(); });
}

void AudioInput::Stop() {
  if (state_ != State::kStarted) {
    return;
  }

  // Tell the worker thread to stop.
  state_ = State::kStopped;

  if (worker_thread_.joinable()) {
    worker_thread_.join();
  }
}

bool AudioInput::can_accept_allocator() const {
  return true;
}

void AudioInput::set_allocator(std::shared_ptr<PayloadAllocator> allocator) {
  allocator_ = allocator;
}

void AudioInput::SetDownstreamDemand(Demand demand) {}

void AudioInput::Worker() {
  FXL_DCHECK(state_ != State::kUninitialized);
  FXL_DCHECK(config_valid_);

  zx_status_t res;
  uint32_t cached_frames_per_packet = frames_per_packet();
  uint32_t cached_packet_size = packet_size();

  // TODO(dalesat): Report this failure so the capture client can be informed.

  auto cleanup = fbl::MakeAutoCall([this]() { audio_input_->Close(); });

  // Open the device
  res = audio_input_->Open();
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed open audio input device (res =" << res << ")";
    return;
  }

  // Configure the format.
  res =
      audio_input_->SetFormat(configured_frames_per_second_,
                              configured_channels_, configured_sample_format_);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed set device format to "
                   << configured_frames_per_second_ << " Hz "
                   << configured_channels_ << " channel"
                   << (configured_channels_ == 1 ? "" : "s") << " fmt "
                   << configured_sample_format_ << " (res " << res << ")";
    return;
  }

  TimelineRate frames_per_sec(audio_input_->frame_rate(), 1);
  TimelineRate sec_per_nsec(1, ZX_SEC(1));
  TimelineRate frames_per_nsec =
      TimelineRate::Product(frames_per_sec, sec_per_nsec);
  TimelineRate nsec_per_frame = frames_per_nsec.Inverse();

  // Establish the shared ring buffer.  Request enough room to hold at least
  // kPacketPerRingBuffer packets.
  uint32_t rb_frame_count = cached_packet_size * kPacketsPerRingBuffer;
  res = audio_input_->GetBuffer(rb_frame_count, 0);
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed fetch ring buffer (" << rb_frame_count
                   << " frames, res = " << res << ")";
    return;
  }

  // Sanity check how much space we actually got.
  FXL_DCHECK(configured_bytes_per_frame_ != 0);
  if (audio_input_->ring_buffer_bytes() % configured_bytes_per_frame_) {
    FXL_LOG(ERROR) << "Error driver supplied ring buffer size ("
                   << audio_input_->ring_buffer_bytes()
                   << ") is not divisible by audio frame size ("
                   << configured_bytes_per_frame_ << ")";
    return;
  }
  rb_frame_count =
      audio_input_->ring_buffer_bytes() / configured_bytes_per_frame_;
  uint32_t rb_packet_count = rb_frame_count / cached_frames_per_packet;

  // Start capturing audio.
  res = audio_input_->StartRingBuffer();
  if (res != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to start capture (res = " << res << ")";
    return;
  }

  // Set up the transformation we will use to map from time to the safe write
  // pointer position in the ring buffer.
  int64_t frames_rxed = 0;
  int64_t fifo_frames =
      (audio_input_->fifo_depth() + configured_bytes_per_frame_ - 1) /
      configured_bytes_per_frame_;

  TimelineFunction clock_mono_to_input_wr_ptr(audio_input_->start_time(),
                                              -fifo_frames, frames_per_nsec);

  while (state_ == State::kStarted) {
    // Steady state operation.  Start by figuring out how many full packets we
    // have waiting for us in the ring buffer.
    uint64_t now = zx_time_get(ZX_CLOCK_MONOTONIC);
    int64_t wr_ptr = clock_mono_to_input_wr_ptr.Apply(now);
    int64_t pending_packets = (wr_ptr - frames_rxed) / cached_frames_per_packet;

    if (pending_packets > 0) {
      // If the number of pending packets is >= the number of packets which can
      // fit into the ring buffer, then we have clearly overflowed.  Print a
      // warning and skip the lost data.
      //
      // TODO(johngro) : We could produce payloads full of silence instead of
      // just skipping the data if we wanted to.  It seems wasteful, however,
      // since clients should be able to infer that data was lost based on the
      // timestamps placed on the packets.
      if (pending_packets >= rb_packet_count) {
        uint32_t skip_count = pending_packets - rb_packet_count + 1;

        FXL_DCHECK(pending_packets > skip_count);
        FXL_LOG(WARNING) << "Input overflowed by " << skip_count << " packets.";
        frames_rxed +=
            static_cast<uint64_t>(skip_count) * cached_frames_per_packet;
        pending_packets -= skip_count;
      }

      // Now produce as many packets as we can given our pending packet count.
      uint32_t modulo_rd_ptr_bytes =
          (frames_rxed % rb_frame_count) * configured_bytes_per_frame_;
      FXL_DCHECK(modulo_rd_ptr_bytes < audio_input_->ring_buffer_bytes());

      while (pending_packets > 0) {
        auto buf = reinterpret_cast<uint8_t*>(
            allocator_->AllocatePayloadBuffer(cached_packet_size));
        if (buf == nullptr) {
          FXL_LOG(ERROR) << "Allocator starved";
          return;
        }

        // Copy the data from the ring buffer into the packet we are producing.
        auto src =
            reinterpret_cast<const uint8_t*>(audio_input_->ring_buffer()) +
            modulo_rd_ptr_bytes;
        uint32_t contig_space =
            audio_input_->ring_buffer_bytes() - modulo_rd_ptr_bytes;
        if (contig_space >= cached_packet_size) {
          ::memcpy(buf, src, cached_packet_size);
          if (contig_space == cached_packet_size) {
            modulo_rd_ptr_bytes = 0;
          } else {
            modulo_rd_ptr_bytes += cached_packet_size;
          }
        } else {
          uint32_t leftover = cached_packet_size - contig_space;
          ::memcpy(buf, src, contig_space);
          ::memcpy(buf + contig_space, audio_input_->ring_buffer(), leftover);
          modulo_rd_ptr_bytes = leftover;
        }
        FXL_DCHECK(modulo_rd_ptr_bytes < audio_input_->ring_buffer_bytes());

        ActiveSourceStage* stage_ptr = stage();
        if (stage_ptr) {
          stage_ptr->SupplyPacket(
              Packet::Create(frames_rxed, frames_per_sec, false, false,
                             cached_packet_size, buf, allocator_));
        }

        // Update our bookkeeping.
        pending_packets--;
        frames_rxed += cached_frames_per_packet;

        // Check to make sure we are not supposed to be stopping at this point.
        if (state_ != State::kStarted) {
          return;
        }
      }

      // TODO(johngro) : If it takes any significant amount of time to produce
      // and push the pending packets, we should re-compute the new position of
      // the write pointer based on the current tick time.
    }

    // Now figure out how long we will need to wait until we have at least one
    // new packet waiting for us in the ring.
    int64_t needed_frames = frames_rxed + cached_frames_per_packet + 1 - wr_ptr;
    int64_t sleep_nsec = nsec_per_frame.Scale(needed_frames);
    if (sleep_nsec > 0) {
      zx_nanosleep(zx_deadline_after(sleep_nsec));
    }
  }
}

}  // namespace media
