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

#ifndef _WMI_H_
#define _WMI_H_

/*
 * This file specifies the WMI interface for the Unified Software
 * Architecture.
 *
 * It includes definitions of all the commands and events. Commands are
 * messages from the host to the target. Events and Replies are messages
 * from the target to the host.
 *
 * Ownership of correctness in regards to WMI commands belongs to the host
 * driver and the target is not required to validate parameters for value,
 * proper range, or any other checking.
 *
 * Guidelines for extending this interface are below.
 *
 * 1. Add new WMI commands ONLY within the specified range - 0x9000 - 0x9fff
 *
 * 2. Use ONLY u32 type for defining member variables within WMI
 *    command/event structures. Do not use u8, u16, bool or
 *    enum types within these structures.
 *
 * 3. DO NOT define bit fields within structures. Implement bit fields
 *    using masks if necessary. Do not use the programming language's bit
 *    field definition.
 *
 * 4. Define macros for encode/decode of u8, u16 fields within
 *    the u32 variables. Use these macros for set/get of these fields.
 *    Try to use this to optimize the structure without bloating it with
 *    u32 variables for every lower sized field.
 *
 * 5. Do not use PACK/UNPACK attributes for the structures as each member
 *    variable is already 4-byte aligned by virtue of being a u32
 *    type.
 *
 * 6. Comment each parameter part of the WMI command/event structure by
 *    using the 2 stars at the beginning of C comment instead of one star to
 *    enable HTML document generation using Doxygen.
 *
 */

/* 95 */
enum wmi_service {
	WMI_SERVICE_BEACON_OFFLOAD = 0,
	WMI_SERVICE_SCAN_OFFLOAD,
	WMI_SERVICE_ROAM_OFFLOAD,
	WMI_SERVICE_BCN_MISS_OFFLOAD,
	WMI_SERVICE_STA_PWRSAVE,
	WMI_SERVICE_STA_ADVANCED_PWRSAVE,
	WMI_SERVICE_AP_UAPSD,
	WMI_SERVICE_AP_DFS,
	WMI_SERVICE_11AC,
	WMI_SERVICE_BLOCKACK,
	WMI_SERVICE_PHYERR,
	WMI_SERVICE_BCN_FILTER,
	WMI_SERVICE_RTT,
	WMI_SERVICE_RATECTRL,
	WMI_SERVICE_WOW,
	WMI_SERVICE_RATECTRL_CACHE,
	WMI_SERVICE_IRAM_TIDS,
	WMI_SERVICE_ARPNS_OFFLOAD,
	WMI_SERVICE_NLO,
	WMI_SERVICE_GTK_OFFLOAD,
	WMI_SERVICE_SCAN_SCH,
	WMI_SERVICE_CSA_OFFLOAD,
	WMI_SERVICE_CHATTER,
	WMI_SERVICE_COEX_FREQAVOID,
	WMI_SERVICE_PACKET_POWER_SAVE,
	WMI_SERVICE_FORCE_FW_HANG,
	WMI_SERVICE_GPIO,
	WMI_SERVICE_STA_DTIM_PS_MODULATED_DTIM,
	WMI_SERVICE_STA_UAPSD_BASIC_AUTO_TRIG,
	WMI_SERVICE_STA_UAPSD_VAR_AUTO_TRIG,
	WMI_SERVICE_STA_KEEP_ALIVE,
	WMI_SERVICE_TX_ENCAP,
	WMI_SERVICE_BURST,
	WMI_SERVICE_SMART_ANTENNA_SW_SUPPORT,
	WMI_SERVICE_SMART_ANTENNA_HW_SUPPORT,
	WMI_SERVICE_ROAM_SCAN_OFFLOAD,
	WMI_SERVICE_AP_PS_DETECT_OUT_OF_SYNC,
	WMI_SERVICE_EARLY_RX,
	WMI_SERVICE_STA_SMPS,
	WMI_SERVICE_FWTEST,
	WMI_SERVICE_STA_WMMAC,
	WMI_SERVICE_TDLS,
	WMI_SERVICE_MCC_BCN_INTERVAL_CHANGE,
	WMI_SERVICE_ADAPTIVE_OCS,
	WMI_SERVICE_BA_SSN_SUPPORT,
	WMI_SERVICE_FILTER_IPSEC_NATKEEPALIVE,
	WMI_SERVICE_WLAN_HB,
	WMI_SERVICE_LTE_ANT_SHARE_SUPPORT,
	WMI_SERVICE_BATCH_SCAN,
	WMI_SERVICE_QPOWER,
	WMI_SERVICE_PLMREQ,
	WMI_SERVICE_THERMAL_MGMT,
	WMI_SERVICE_RMC,
	WMI_SERVICE_MHF_OFFLOAD,
	WMI_SERVICE_COEX_SAR,
	WMI_SERVICE_BCN_TXRATE_OVERRIDE,
	WMI_SERVICE_NAN,
	WMI_SERVICE_L1SS_STAT,
	WMI_SERVICE_ESTIMATE_LINKSPEED,
	WMI_SERVICE_OBSS_SCAN,
	WMI_SERVICE_TDLS_OFFCHAN,
	WMI_SERVICE_TDLS_UAPSD_BUFFER_STA,
	WMI_SERVICE_TDLS_UAPSD_SLEEP_STA,
	WMI_SERVICE_IBSS_PWRSAVE,
	WMI_SERVICE_LPASS,
	WMI_SERVICE_EXTSCAN,
	WMI_SERVICE_D0WOW,
	WMI_SERVICE_HSOFFLOAD,
	WMI_SERVICE_ROAM_HO_OFFLOAD,
	WMI_SERVICE_RX_FULL_REORDER,
	WMI_SERVICE_DHCP_OFFLOAD,
	WMI_SERVICE_STA_RX_IPA_OFFLOAD_SUPPORT,
	WMI_SERVICE_MDNS_OFFLOAD,
	WMI_SERVICE_SAP_AUTH_OFFLOAD,
	WMI_SERVICE_ATF,
	WMI_SERVICE_COEX_GPIO,
	WMI_SERVICE_ENHANCED_PROXY_STA,
	WMI_SERVICE_TT,
	WMI_SERVICE_PEER_CACHING,
	WMI_SERVICE_AUX_SPECTRAL_INTF,
	WMI_SERVICE_AUX_CHAN_LOAD_INTF,
	WMI_SERVICE_BSS_CHANNEL_INFO_64,
	WMI_SERVICE_EXT_RES_CFG_SUPPORT,
	WMI_SERVICE_MESH_11S,
	WMI_SERVICE_MESH_NON_11S,
	WMI_SERVICE_PEER_STATS,
	WMI_SERVICE_RESTRT_CHNL_SUPPORT,
	WMI_SERVICE_PERIODIC_CHAN_STAT_SUPPORT,
	WMI_SERVICE_TX_MODE_PUSH_ONLY,
	WMI_SERVICE_TX_MODE_PUSH_PULL,
	WMI_SERVICE_TX_MODE_DYNAMIC,

	/* keep last */
	WMI_SERVICE_MAX,
};

/* 1854 */
#define WMI_MAX_SPATIAL_STREAM        3 /* default max ss */

/* 4153 */
enum wmi_stats_id {
	WMI_STAT_PEER = BIT(0),
	WMI_STAT_AP = BIT(1),
	WMI_STAT_PDEV = BIT(2),
	WMI_STAT_VDEV = BIT(3),
	WMI_STAT_BCNFLT = BIT(4),
	WMI_STAT_VDEV_RATE = BIT(5),
};

/* 4162 */
enum wmi_10_4_stats_id {
        WMI_10_4_STAT_PEER              = BIT(0),
        WMI_10_4_STAT_AP                = BIT(1),
        WMI_10_4_STAT_INST              = BIT(2),
        WMI_10_4_STAT_PEER_EXTD         = BIT(3),
};

#endif /* _WMI_H_ */
