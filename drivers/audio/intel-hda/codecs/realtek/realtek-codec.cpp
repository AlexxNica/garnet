// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mxtl/auto_lock.h>

#include "drivers/audio/dispatcher-pool/dispatcher-thread.h"

#include "debug-logging.h"
#include "realtek-codec.h"
#include "realtek-stream.h"

namespace audio {
namespace intel_hda {
namespace codecs {

static constexpr float DEFAULT_HEADPHONE_GAIN = -30.0;
static constexpr float DEFAULT_SPEAKER_GAIN = 0.0;

void RealtekCodec::PrintDebugPrefix() const {
    printf("RealtekCodec : ");
}

mxtl::RefPtr<RealtekCodec> RealtekCodec::Create() {
    return mxtl::AdoptRef(new RealtekCodec);
}

mx_status_t RealtekCodec::Init(mx_driver_t* driver, mx_device_t* codec_dev) {
    mx_status_t res = Bind(driver, codec_dev);
    if (res != NO_ERROR)
        return res;

    res = Start();
    if (res != NO_ERROR) {
        Shutdown();
        return res;
    }

    return NO_ERROR;
}

mx_status_t RealtekCodec::Start() {
    mx_status_t res;

    // Fetch the implementation ID register from the main audio function group
    res = SendCodecCommand(1u, GET_IMPLEMENTATION_ID, false);
    if (res != NO_ERROR)
        LOG("Failed to send get impl id command (res %d)\n", res);
    return res;
}

mx_status_t RealtekCodec::ProcessSolicitedResponse(const CodecResponse& resp) {
    if (!waiting_for_impl_id_) {
        LOG("Unexpected solicited codec response %08x\n", resp.data);
        return ERR_BAD_STATE;
    }

    waiting_for_impl_id_ = false;

    // TODO(johngro) : Don't base this setup behavior on exact matches in the
    // implementation ID register.  We should move in the direction of
    // implementing a universal driver which depends mostly on codec VID/DID and
    // BIOS provided configuration hints to make the majority of configuration
    // decisions, and to rely on the impl ID as little as possible.
    //
    // At the very least, we should break this field down into its sub-fields
    // (mfr ID, board SKU, assembly ID) and match based on those.  I'm willing
    // to bet that not all NUCs in the world are currently using the exact
    // same bits for this register.
    mx_status_t res;
    switch (resp.data) {
        // Intel NUC
        case 0x80862063: res = SetupIntelNUC(); break;
        case 0x1025111e: res = SetupAcer12(); break;
        default:
            LOG("Unrecognized implementation ID %08x!  No streams will be published.\n", resp.data);
            res = NO_ERROR;
            break;
    }

    // TODO(johngro) : Begin the process of tearing down and cleaning up if setup fails
    return res;
}

mx_status_t RealtekCodec::SetupCommon() {
    // Common startup commands
    static const CommandListEntry START_CMDS[] = {
        // Start powering down the function group.
        {  1u, SET_POWER_STATE(HDA_PS_D3HOT) },

        // Converters.  Place all converters into D3HOT and mute/attenuate their outputs.
        // Output converters.
        {  2u, SET_POWER_STATE(HDA_PS_D3HOT) },
        {  2u, SET_OUTPUT_AMPLIFIER_GAIN_MUTE(false, 0), },
        {  3u, SET_POWER_STATE(HDA_PS_D3HOT) },
        {  3u, SET_OUTPUT_AMPLIFIER_GAIN_MUTE(false, 0), },
        {  6u, SET_POWER_STATE(HDA_PS_D3HOT) },

        // Input converters.
        {  8u, SET_POWER_STATE(HDA_PS_D3HOT) },
        {  8u, SET_INPUT_AMPLIFIER_GAIN_MUTE(true, 0), },
        {  9u, SET_POWER_STATE(HDA_PS_D3HOT) },
        {  9u, SET_INPUT_AMPLIFIER_GAIN_MUTE(true, 0), },

        // Pin complexes.  Place all complexes into powered down states.  Disable all
        // inputs/outputs/external amps, etc...

        // DMIC input
        { 18u, SET_POWER_STATE(HDA_PS_D3HOT) }, // Input
        { 18u, SET_ANALOG_PIN_WIDGET_CTRL(false, false, false) },

        // Class-D Power Amp output
        { 20u, SET_POWER_STATE(HDA_PS_D3HOT) },
        { 20u, SET_OUTPUT_AMPLIFIER_GAIN_MUTE(true, 0), },
        { 20u, SET_ANALOG_PIN_WIDGET_CTRL(false, false, false) },
        { 20u, SET_EAPD_BTL_ENABLE(0) },

        // Mono output
        { 23u, SET_POWER_STATE(HDA_PS_D3HOT) },
        { 23u, SET_OUTPUT_AMPLIFIER_GAIN_MUTE(true, 0), },
        { 23u, SET_ANALOG_PIN_WIDGET_CTRL(false, false, false) },

        // Undocumented input...
        { 24u, SET_POWER_STATE(HDA_PS_D3HOT) },
        { 24u, SET_INPUT_AMPLIFIER_GAIN_MUTE(false, 0), },
        { 24u, SET_ANALOG_PIN_WIDGET_CTRL(false, false, false) },

        // MIC2 input
        { 25u, SET_POWER_STATE(HDA_PS_D3HOT) },
        { 25u, SET_INPUT_AMPLIFIER_GAIN_MUTE(false, 0), },
        { 25u, SET_ANALOG_PIN_WIDGET_CTRL(false, false, false) },

        // LINE1 input
        { 26u, SET_POWER_STATE(HDA_PS_D3HOT) },
        { 26u, SET_INPUT_AMPLIFIER_GAIN_MUTE(false, 0), },
        { 26u, SET_ANALOG_PIN_WIDGET_CTRL(false, false, false) },

        // LINE2 in/out
        { 27u, SET_POWER_STATE(HDA_PS_D3HOT) },
        { 27u, SET_INPUT_AMPLIFIER_GAIN_MUTE(false, 0), },
        { 27u, SET_OUTPUT_AMPLIFIER_GAIN_MUTE(true, 0), },
        { 27u, SET_ANALOG_PIN_WIDGET_CTRL(false, false, false) },
        { 27u, SET_EAPD_BTL_ENABLE(0) },

        // PC Beep input
        { 29u, SET_POWER_STATE(HDA_PS_D3HOT) },
        { 29u, SET_ANALOG_PIN_WIDGET_CTRL(false, false, false) },

        // S/PDIF out
        { 30u, SET_POWER_STATE(HDA_PS_D3HOT) },
        { 30u, SET_DIGITAL_PIN_WIDGET_CTRL(false, false) },

        // Headphone out
        { 33u, SET_POWER_STATE(HDA_PS_D3HOT) },
        { 33u, SET_OUTPUT_AMPLIFIER_GAIN_MUTE(true, 0), },
        { 33u, SET_ANALOG_PIN_WIDGET_CTRL(false, false, false) },
        { 33u, SET_EAPD_BTL_ENABLE(0) },
    };

    mx_status_t res = RunCommandList(START_CMDS, countof(START_CMDS));

    if (res != NO_ERROR)
        LOG("Failed to send common startup commands (res %d)\n", res);

    return res;
}

mx_status_t RealtekCodec::SetupAcer12() {
    mx_status_t res;

    DEBUG_LOG("Setting up for Acer12\n");

    res = SetupCommon();
    if (res != NO_ERROR)
        return res;

    static const CommandListEntry START_CMDS[] = {
        // Set up the routing that we will use for the headphone output.
        { 13u, SET_OUTPUT_AMPLIFIER_GAIN_MUTE(false, 0, 0), },  // Mix NID 13, In-0 (nid 3) un-muted
        { 13u, SET_OUTPUT_AMPLIFIER_GAIN_MUTE(true,  1, 0), },  // Mix NID 13, In-1 (nid 11) muted
        { 33u, SET_CONNECTION_SELECT_CONTROL(1u) },             // HP Pin source from ndx 0 (nid 13)

        // Set up the routing that we will use for the speaker output.
        { 12u, SET_OUTPUT_AMPLIFIER_GAIN_MUTE(false, 0, 0), },  // Mix NID 12, In-0 (nid 2) un-muted
        { 12u, SET_OUTPUT_AMPLIFIER_GAIN_MUTE(true,  1, 0), },  // Mix NID 12, In-1 (nid 11) muted

        // Enable MIC2's input.  Failure to do this causes the positive half of
        // the headphone output to be destroyed.
        //
        // TODO(johngro) : figure out why
        { 25u, SET_ANALOG_PIN_WIDGET_CTRL(false, true, false) },

        // Power up the top level Audio Function group.
        {  1u, SET_POWER_STATE(HDA_PS_D0) },
    };

    res = RunCommandList(START_CMDS, countof(START_CMDS));
    if (res != NO_ERROR) {
        LOG("Failed to send startup command for Acer12 (res %d)\n", res);
        return res;
    }

    // Create and publish the streams we will use.
    static const StreamProperties STREAMS[] = {
        // Headphones
        { .stream_id    = 1,
          .afg_nid      = 1,
          .conv_nid     = 3,
          .pc_nid       = 33,
          .is_input     = false,
          .default_gain = DEFAULT_HEADPHONE_GAIN,
        },

        // Speakers
        { .stream_id    = 2,
          .afg_nid      = 1,
          .conv_nid     = 2,
          .pc_nid       = 20,
          .is_input     = false,
          .default_gain = DEFAULT_SPEAKER_GAIN,
        },
    };

    res = CreateAndStartStreams(STREAMS, countof(STREAMS));
    if (res != NO_ERROR) {
        LOG("Failed to create and publish streams for Acer12 (res %d)\n", res);
        return res;
    }

    return NO_ERROR;
}

mx_status_t RealtekCodec::SetupIntelNUC() {
    mx_status_t res;

    DEBUG_LOG("Setting up for Intel NUC\n");

    res = SetupCommon();
    if (res != NO_ERROR)
        return res;

    static const CommandListEntry START_CMDS[] = {
        // Set up the routing that we will use for the headphone output.
        { 12u, SET_OUTPUT_AMPLIFIER_GAIN_MUTE(false, 0, 0), },  // Mix NID 12, In-0 (nid 2) un-muted
        { 12u, SET_OUTPUT_AMPLIFIER_GAIN_MUTE(true,  1, 0), },  // Mix NID 12, In-1 (nid 11) muted
        { 33u, SET_CONNECTION_SELECT_CONTROL(0u) },             // HP Pin source from ndx 0 (nid 12)

        // Enable MIC2's input.  Failure to do this causes the positive half of
        // the headphone output to be destroyed.
        //
        // TODO(johngro) : figure out why
        { 25u, SET_ANALOG_PIN_WIDGET_CTRL(false, true, false) },

        // Power up the top level Audio Function group.
        {  1u, SET_POWER_STATE(HDA_PS_D0) },
    };

    res = RunCommandList(START_CMDS, countof(START_CMDS));
    if (res != NO_ERROR) {
        LOG("Failed to send startup command for Intel NUC (res %d)\n", res);
        return res;
    }

    // Create and publish the streams we will use.
    static const StreamProperties STREAMS[] = {
        // Headphones
        { .stream_id    = 1,
          .afg_nid      = 1,
          .conv_nid     = 2,
          .pc_nid       = 33,
          .is_input     = false,
          .default_gain = DEFAULT_HEADPHONE_GAIN,
        },
    };

    res = CreateAndStartStreams(STREAMS, countof(STREAMS));
    if (res != NO_ERROR) {
        LOG("Failed to create and publish streams for Intel NUC (res %d)\n", res);
        return res;
    }

    return NO_ERROR;
}

mx_status_t RealtekCodec::RunCommandList(const CommandListEntry* cmds, size_t cmd_count) {
    mx_status_t res;

    if (cmds == nullptr)
        return ERR_INVALID_ARGS;

    for (size_t i = 0; i < cmd_count; ++i) {
        const auto& cmd = cmds[i];
        VERBOSE_LOG("SEND: nid %2hu verb 0x%05x\n", cmd.nid, cmd.verb.val);
        res = SendCodecCommand(cmd.nid, cmd.verb, true);
        if (res != NO_ERROR) {
            LOG("Failed to send codec command %zu/%zu (nid %hu verb 0x%05x) (res %d)\n",
                i + 1, cmd_count, cmd.nid, cmd.verb.val, res);
            return res;
        }
    }

    return NO_ERROR;
}

mx_status_t RealtekCodec::CreateAndStartStreams(const StreamProperties* streams,
                                                size_t stream_cnt) {
    mx_status_t res;

    if (streams == nullptr)
        return ERR_INVALID_ARGS;

    for (size_t i = 0; i < stream_cnt; ++i) {
        const auto& stream_def = streams[i];
        auto stream = mxtl::AdoptRef(new RealtekStream(stream_def));

        res = ActivateStream(stream);
        if (res != NO_ERROR) {
            LOG("Failed to activate %s stream id #%u (res %d)!",
                 stream_def.is_input ? "input" : "output", stream_def.stream_id, res);
            return res;
        }
    }

    return NO_ERROR;
}

extern "C" mx_status_t realtek_ihda_codec_bind_hook(mx_driver_t* driver,
                                                    mx_device_t* codec_dev,
                                                    void** cookie) {
    if (cookie == nullptr)
        return ERR_INVALID_ARGS;

    auto codec = RealtekCodec::Create();
    MX_DEBUG_ASSERT(codec != nullptr);

    // Init our codec.  If we succeed, transfer our reference to the unmanaged
    // world.  We will re-claim it later when unbind is called.
    mx_status_t res = codec->Init(driver, codec_dev);
    if (res == NO_ERROR)
        *cookie = codec.leak_ref();

    return res;
}

extern "C" void realtek_ihda_codec_unbind_hook(mx_driver_t* driver,
                                               mx_device_t* codec_dev,
                                               void* cookie) {
    MX_DEBUG_ASSERT(cookie != nullptr);

    // Reclaim our reference from the cookie.
    auto codec = mxtl::internal::MakeRefPtrNoAdopt(reinterpret_cast<RealtekCodec*>(cookie));

    // Shut the codec down.
    codec->Shutdown();

    // Let go of the reference.
    codec.reset();

    // Signal the thread pool so it can completely shut down if we were the last client.
    DispatcherThread::ShutdownThreadPool();
}

}  // namespace codecs
}  // namespace audio
}  // namespace intel_hda

