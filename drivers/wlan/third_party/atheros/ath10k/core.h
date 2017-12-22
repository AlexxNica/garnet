/*
 * Copyright (c) 2005-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2013 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _CORE_H_
#define _CORE_H_

#define _ALL_SOURCE
#include <stdatomic.h>
#include <threads.h>
#include <pthread.h>

#include <ddk/device.h>

#include "linuxisms.h"
#include "htt.h"
#include "hw.h"
#include "targaddrs.h"
#include "wmi.h"
#include "../ath.h"
#include "wow.h"

/* 41 */
#define MS(_v, _f) (((_v) & _f##_MASK) >> _f##_LSB)
#define SM(_v, _f) (((_v) << _f##_LSB) & _f##_MASK)
#define WO(_f)      ((_f##_OFFSET) >> 2)

/* 91 */
enum ath10k_bus {
        ATH10K_BUS_PCI,
        ATH10K_BUS_AHB,
        ATH10K_BUS_SDIO,
};

/* 97 */
static inline const char *ath10k_bus_str(enum ath10k_bus bus)
{
        switch (bus) {
        case ATH10K_BUS_PCI:
                return "pci";
        case ATH10K_BUS_AHB:
                return "ahb";
        case ATH10K_BUS_SDIO:
                return "sdio";
        }

        return "unknown";
}

/* 149 */
static inline uint32_t host_interest_item_address(uint32_t item_offset)
{
        return QCA988X_HOST_INTEREST_ADDRESS + item_offset;
}

/* 154 */
struct ath10k_bmi {
        bool done_sent;
};

/* 165 */
struct ath10k_wmi {
	uint32_t rx_decap_mode;
	uint8_t svc_map[WMI_SERVICE_MAX];	// TODO - convert to a proper multi-word bitfield
};

/* 443 */
/* Copy Engine register dump, protected by ce-lock */
struct ath10k_ce_crash_data {
        uint32_t base_addr;
        uint32_t src_wr_idx;
        uint32_t src_r_idx;
        uint32_t dst_wr_idx;
        uint32_t dst_r_idx;
};

struct ath10k_ce_crash_hdr {
        uint32_t ce_count;
        uint32_t reserved[3]; /* for future use */
        struct ath10k_ce_crash_data entries[];
};

/* used for crash-dump storage, protected by data-lock */
struct ath10k_fw_crash_data {
        bool crashed_since_read;

        uint8_t uuid[16];
        struct timespec timestamp;
        uint32_t registers[REG_DUMP_COUNT_QCA988X];
        struct ath10k_ce_crash_data ce_crash_data[CE_COUNT_MAX];
};

/* 496 */
enum ath10k_state {
	ATH10K_STATE_OFF = 0,
	ATH10K_STATE_ON,

	/* When doing firmware recovery the device is first powered down.
	 * mac80211 is supposed to call in to start() hook later on. It is
	 * however possible that driver unloading and firmware crash overlap.
	 * mac80211 can wait on conf_mutex in stop() while the device is
	 * stopped in ath10k_core_restart() work holding conf_mutex. The state
	 * RESTARTED means that the device is up and mac80211 has started hw
	 * reconfiguration. Once mac80211 is done with the reconfiguration we
	 * set the state to STATE_ON in reconfig_complete().
	 */
	ATH10K_STATE_RESTARTING,
	ATH10K_STATE_RESTARTED,

	/* The device has crashed while restarting hw. This state is like ON
	 * but commands are blocked in HTC and -ECOMM response is given. This
	 * prevents completion timeouts and makes the driver more responsive to
	 * userspace commands. This is also prevents recursive recovery.
	 */
	ATH10K_STATE_WEDGED,

	/* factory tests */
	ATH10K_STATE_UTF,
};

/* 523 */
enum ath10k_firmware_mode {
        /* the default mode, standard 802.11 functionality */
        ATH10K_FIRMWARE_MODE_NORMAL,

        /* factory tests etc */
        ATH10K_FIRMWARE_MODE_UTF,
};

/* 531 */
enum ath10k_fw_features {
        /* wmi_mgmt_rx_hdr contains extra RSSI information */
        ATH10K_FW_FEATURE_EXT_WMI_MGMT_RX = 0,

        /* Firmware from 10X branch. Deprecated, don't use in new code. */
        ATH10K_FW_FEATURE_WMI_10X = 1,

        /* firmware support tx frame management over WMI, otherwise it's HTT */
        ATH10K_FW_FEATURE_HAS_WMI_MGMT_TX = 2,

        /* Firmware does not support P2P */
        ATH10K_FW_FEATURE_NO_P2P = 3,

        /* Firmware 10.2 feature bit. The ATH10K_FW_FEATURE_WMI_10X feature
         * bit is required to be set as well. Deprecated, don't use in new
         * code.
         */
        ATH10K_FW_FEATURE_WMI_10_2 = 4,

        /* Some firmware revisions lack proper multi-interface client powersave
         * implementation. Enabling PS could result in connection drops,
         * traffic stalls, etc.
         */
        ATH10K_FW_FEATURE_MULTI_VIF_PS_SUPPORT = 5,

        /* Some firmware revisions have an incomplete WoWLAN implementation
         * despite WMI service bit being advertised. This feature flag is used
         * to distinguish whether WoWLAN is really supported or not.
         */
        ATH10K_FW_FEATURE_WOWLAN_SUPPORT = 6,

        /* Don't trust error code from otp.bin */
        ATH10K_FW_FEATURE_IGNORE_OTP_RESULT = 7,

        /* Some firmware revisions pad 4th hw address to 4 byte boundary making
         * it 8 bytes long in Native Wifi Rx decap.
         */
        ATH10K_FW_FEATURE_NO_NWIFI_DECAP_4ADDR_PADDING = 8,

        /* Firmware supports bypassing PLL setting on init. */
        ATH10K_FW_FEATURE_SUPPORTS_SKIP_CLOCK_INIT = 9,

        /* Raw mode support. If supported, FW supports receiving and trasmitting
         * frames in raw mode.
         */
        ATH10K_FW_FEATURE_RAW_MODE_SUPPORT = 10,

        /* Firmware Supports Adaptive CCA*/
        ATH10K_FW_FEATURE_SUPPORTS_ADAPTIVE_CCA = 11,

        /* Firmware supports management frame protection */
        ATH10K_FW_FEATURE_MFP_SUPPORT = 12,

        /* Firmware supports pull-push model where host shares it's software
         * queue state with firmware and firmware generates fetch requests
         * telling host which queues to dequeue tx from.
         *
         * Primary function of this is improved MU-MIMO performance with
         * multiple clients.
         */
        ATH10K_FW_FEATURE_PEER_FLOW_CONTROL = 13,

        /* Firmware supports BT-Coex without reloading firmware via pdev param.
         * To support Bluetooth coexistence pdev param, WMI_COEX_GPIO_SUPPORT of
         * extended resource config should be enabled always. This firmware IE
         * is used to configure WMI_COEX_GPIO_SUPPORT.
         */
        ATH10K_FW_FEATURE_BTCOEX_PARAM = 14,

        /* Unused flag and proven to be not working, enable this if you want
         * to experiment sending NULL func data frames in HTT TX
         */
        ATH10K_FW_FEATURE_SKIP_NULL_FUNC_WAR = 15,

        /* Firmware allow other BSS mesh broadcast/multicast frames without
         * creating monitor interface. Appropriate rxfilters are programmed for
         * mesh vdev by firmware itself. This feature flags will be used for
         * not creating monitor vdev while configuring mesh node.
         */
        ATH10K_FW_FEATURE_ALLOWS_MESH_BCAST = 16,

        /* keep last */
        ATH10K_FW_FEATURE_COUNT,
};

/* 616 */
enum ath10k_dev_flags {
        /* Indicates that ath10k device is during CAC phase of DFS */
        ATH10K_CAC_RUNNING = 1 << 0,
        ATH10K_FLAG_CORE_REGISTERED = 1 << 1,

        /* Device has crashed and needs to restart. This indicates any pending
         * waiters should immediately cancel instead of waiting for a time out.
         */
        ATH10K_FLAG_CRASH_FLUSH = 1 << 2,

        /* Use Raw mode instead of native WiFi Tx/Rx encap mode.
         * Raw mode supports both hardware and software crypto. Native WiFi only
         * supports hardware crypto.
         */
        ATH10K_FLAG_RAW_MODE = 1 << 3,

        /* Disable HW crypto engine */
        ATH10K_FLAG_HW_CRYPTO_DISABLED = 1 << 4,

        /* Bluetooth coexistance enabled */
        ATH10K_FLAG_BTCOEX = 1 << 5,

        /* Per Station statistics service */
        ATH10K_FLAG_PEER_STATS = 1 << 6,
};

/* 642 */
enum ath10k_cal_mode {
        ATH10K_CAL_MODE_FILE,
        ATH10K_CAL_MODE_OTP,
        ATH10K_PRE_CAL_MODE_FILE,
        ATH10K_CAL_MODE_EEPROM,
};

/* 651 */
enum ath10k_crypt_mode {
        /* Only use hardware crypto engine */
        ATH10K_CRYPT_MODE_HW,
        /* Only use software crypto engine */
        ATH10K_CRYPT_MODE_SW,
};

/* 658 */
static inline const char *ath10k_cal_mode_str(enum ath10k_cal_mode mode)
{
        switch (mode) {
        case ATH10K_CAL_MODE_FILE:
                return "file";
        case ATH10K_CAL_MODE_OTP:
                return "otp";
        case ATH10K_PRE_CAL_MODE_FILE:
                return "pre-cal-file";
        case ATH10K_CAL_MODE_EEPROM:
                return "eeprom";
        }

        return "unknown";
}

/* Fuchsia */
struct ath10k_firmware {
	zx_handle_t vmo;
	uint8_t *data;
	size_t size;
};

/* 706 */
struct ath10k_fw_file {
	struct ath10k_firmware firmware;
	size_t firmware_size;

        char fw_version[ETHTOOL_FWVERS_LEN];

        uint64_t fw_features;

        enum ath10k_fw_wmi_op_version wmi_op_version;
        enum ath10k_fw_htt_op_version htt_op_version;

        const void *firmware_data;
        size_t firmware_len;

        const void *otp_data;
        size_t otp_len;

        const void *codeswap_data;
        size_t codeswap_len;

        /* The original idea of struct ath10k_fw_file was that it only
         * contains struct firmware and pointers to various parts (actual
         * firmware binary, otp, metadata etc) of the file. This seg_info
         * is actually created separate but as this is used similarly as
         * the other firmware components it's more convenient to have it
         * here.
         */
        struct ath10k_swap_code_seg_info *firmware_swap_code_seg_info;
};

/* 735 */
struct ath10k_fw_components {
	struct ath10k_firmware board;
	const void *board_data;
	size_t board_len;

        struct ath10k_fw_file fw_file;
};

/* 758 */
struct ath10k {
	struct ath_common ath_common;

	/* Fuchsia */
	zx_device_t* zxdev;
	thrd_t init_thread;

	/* 765 */
	enum ath10k_hw_rev hw_rev;
	uint16_t dev_id;
	uint32_t chip_id;
	uint32_t target_version;
	uint32_t fw_stats_req_mask;
	uint32_t max_spatial_stream;

	/* 789 */
        struct {
                enum ath10k_bus bus;
                const struct ath10k_hif_ops *ops;
        } hif;

	/* 796 */
        const struct ath10k_hw_regs *regs;
        const struct ath10k_hw_ce_regs *hw_ce_regs;
        const struct ath10k_hw_values *hw_values;
	struct ath10k_bmi bmi;
	struct ath10k_wmi wmi;
	struct ath10k_htt htt;

	/* 804 */
	struct ath10k_hw_params hw_params;

        /* contains the firmware images used with ATH10K_FIRMWARE_MODE_NORMAL */
        struct ath10k_fw_components normal_mode_fw;

	/* 814 */
	struct ath10k_firmware pre_cal_file;
	struct ath10k_firmware cal_file;

	/* 817 */
        struct {
                uint32_t vendor;
                uint32_t device;
		uint32_t subsystem_vendor;
		uint32_t subsystem_device;

		bool bmi_ids_valid;
		uint8_t bmi_board_id;
		uint8_t bmi_chip_id;
        } id;

	/* 830 */
	int fw_api;
	int bd_api;
	enum ath10k_cal_mode cal_mode;

	/* 868 */
	atomic_ulong dev_flags;

	/* 887 */
	/* prevents concurrent FW reconfiguration */
	mtx_t conf_mutex;

        /* protects shared structure data */
        pthread_spinlock_t data_lock;
        /* protects: ar->txqs, artxq->list */
        pthread_spinlock_t txqs_lock;

	/* 895 */
	list_node_t txqs;

	/* 897 */
	list_node_t peers;

	/* 905 */
	int max_num_peers;
	int max_num_stations;
	int max_num_vdevs;
	int max_num_tdls_vdevs;
	int num_active_peers;
	int num_tids;

	/* 923 */
	enum ath10k_state state;

	/* 925 */
	thrd_t register_work;

	/* 968 */
        struct {
                /* protected by data_lock */
                uint32_t fw_crash_counter;
                uint32_t fw_warm_reset_counter;
                uint32_t fw_cold_reset_counter;
        } stats;

	/* 976 */
	struct ath10k_wow wow;

	/* 996 */
        /* must be last */
        void* drv_priv;
};

/* 1000 */
static inline bool ath10k_peer_stats_enabled(struct ath10k *ar)
{
	if ((ar->dev_flags & ATH10K_FLAG_PEER_STATS) && 
	    ar->wmi.svc_map[WMI_SERVICE_PEER_STATS]) {
                return true;
	}

        return false;
}

/* 1009 */
zx_status_t ath10k_core_create(struct ath10k **ar_ptr, size_t priv_size,
				  zx_device_t *dev, enum ath10k_bus bus,
                                  enum ath10k_hw_rev hw_rev,
                                  const struct ath10k_hif_ops *hif_ops);
void ath10k_core_destroy(struct ath10k *ar);

/* 1017 */
zx_status_t ath10k_core_fetch_firmware_api_n(struct ath10k *ar, const char *name,
                                             struct ath10k_fw_file *fw_file);

/* 1024 */
zx_status_t ath10k_core_register(struct ath10k *ar, uint32_t chip_id);

#endif /* _CORE_H_ */
