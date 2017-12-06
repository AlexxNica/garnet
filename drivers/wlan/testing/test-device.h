// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/test.h>
#include <ddktl/device.h>
#include <ddktl/protocol/test.h>
#include <ddktl/protocol/wlan.h>
#include <fbl/unique_ptr.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <mutex>

namespace wlan {
namespace testing {

class Device;
using TestBaseDevice = ddk::Device<Device, ddk::Unbindable, ddk::Ioctlable>;

class Device : public TestBaseDevice, public ddk::WlanmacProtocol<Device> {
   public:
    Device(zx_device_t* device, test_protocol_t* test_proto);

    zx_status_t Bind();

    void DdkUnbind();
    void DdkRelease();
    zx_status_t DdkIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                         size_t out_len, size_t* out_actual);

    zx_status_t WlanmacQuery(uint32_t options, wlanmac_info_t* info);
    void WlanmacStop();
    zx_status_t WlanmacStart(fbl::unique_ptr<ddk::WlanmacIfcProxy> proxy);
    zx_status_t WlanmacQueueTx(uint32_t options, wlan_tx_packet_t* pkt);
    zx_status_t WlanmacSetChannel(uint32_t options, wlan_channel_t* chan);
    zx_status_t WlanmacSetBss(uint32_t options, const uint8_t mac[6], uint8_t type);
    zx_status_t WlanmacSetKey(uint32_t options, wlan_key_config_t* key_config);

   private:
    ddk::TestProtocolProxy test_proxy_;

    std::mutex lock_;
    fbl::unique_ptr<ddk::WlanmacIfcProxy> wlanmac_proxy_ __TA_GUARDED(lock_);
};

}  // namespace testing
}  // namespace wlan
