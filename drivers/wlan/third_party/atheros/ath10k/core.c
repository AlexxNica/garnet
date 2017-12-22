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

#include <string.h>

#include <zircon/assert.h>
#include <zircon/status.h>

#include "core.h"
#include "mac.h"
// #include "htc.h"
#include "hif.h"
// #include "wmi.h"
#include "bmi.h"
#include "debug.h"
// #include "htt.h"
// #include "testmode.h"
// #include "wmi-ops.h"
#include "linuxisms.h"

/* 54 */
static const struct ath10k_hw_params ath10k_hw_params_list[] = {
	{
		.id = QCA988X_HW_2_0_VERSION,
		.dev_id = QCA988X_2_0_DEVICE_ID,
		.name = "qca988x hw2.0",
		.patch_load_addr = QCA988X_HW_2_0_PATCH_LOAD_ADDR,
		.uart_pin = 7,
		.cc_wraparound_type = ATH10K_HW_CC_WRAP_SHIFTED_ALL,
		.otp_exe_param = 0,
		.channel_counters_freq_hz = 88000,
		.max_probe_resp_desc_thres = 0,
		.cal_data_len = 2116,
		.fw = {
			.dir = QCA988X_HW_2_0_FW_DIR,
			.board = QCA988X_HW_2_0_BOARD_DATA_FILE,
			.board_size = QCA988X_BOARD_DATA_SZ,
			.board_ext_size = QCA988X_BOARD_EXT_DATA_SZ,
		},
		.hw_ops = &qca988x_ops,
		.decap_align_bytes = 4,
		.spectral_bin_discard = 0,
		.vht160_mcs_rx_highest = 0,
		.vht160_mcs_tx_highest = 0,
	},
	{
		.id = QCA9887_HW_1_0_VERSION,
		.dev_id = QCA9887_1_0_DEVICE_ID,
		.name = "qca9887 hw1.0",
		.patch_load_addr = QCA9887_HW_1_0_PATCH_LOAD_ADDR,
		.uart_pin = 7,
		.cc_wraparound_type = ATH10K_HW_CC_WRAP_SHIFTED_ALL,
		.otp_exe_param = 0,
		.channel_counters_freq_hz = 88000,
		.max_probe_resp_desc_thres = 0,
		.cal_data_len = 2116,
		.fw = {
			.dir = QCA9887_HW_1_0_FW_DIR,
			.board = QCA9887_HW_1_0_BOARD_DATA_FILE,
			.board_size = QCA9887_BOARD_DATA_SZ,
			.board_ext_size = QCA9887_BOARD_EXT_DATA_SZ,
		},
		.hw_ops = &qca988x_ops,
		.decap_align_bytes = 4,
		.spectral_bin_discard = 0,
		.vht160_mcs_rx_highest = 0,
		.vht160_mcs_tx_highest = 0,
	},
	{
		.id = QCA6174_HW_2_1_VERSION,
		.dev_id = QCA6164_2_1_DEVICE_ID,
		.name = "qca6164 hw2.1",
		.patch_load_addr = QCA6174_HW_2_1_PATCH_LOAD_ADDR,
		.uart_pin = 6,
		.otp_exe_param = 0,
		.channel_counters_freq_hz = 88000,
		.max_probe_resp_desc_thres = 0,
		.cal_data_len = 8124,
		.fw = {
			.dir = QCA6174_HW_2_1_FW_DIR,
			.board = QCA6174_HW_2_1_BOARD_DATA_FILE,
			.board_size = QCA6174_BOARD_DATA_SZ,
			.board_ext_size = QCA6174_BOARD_EXT_DATA_SZ,
		},
		.hw_ops = &qca988x_ops,
		.decap_align_bytes = 4,
		.spectral_bin_discard = 0,
		.vht160_mcs_rx_highest = 0,
		.vht160_mcs_tx_highest = 0,
	},
	{
		.id = QCA6174_HW_2_1_VERSION,
		.dev_id = QCA6174_2_1_DEVICE_ID,
		.name = "qca6174 hw2.1",
		.patch_load_addr = QCA6174_HW_2_1_PATCH_LOAD_ADDR,
		.uart_pin = 6,
		.otp_exe_param = 0,
		.channel_counters_freq_hz = 88000,
		.max_probe_resp_desc_thres = 0,
		.cal_data_len = 8124,
		.fw = {
			.dir = QCA6174_HW_2_1_FW_DIR,
			.board = QCA6174_HW_2_1_BOARD_DATA_FILE,
			.board_size = QCA6174_BOARD_DATA_SZ,
			.board_ext_size = QCA6174_BOARD_EXT_DATA_SZ,
		},
		.hw_ops = &qca988x_ops,
		.decap_align_bytes = 4,
		.spectral_bin_discard = 0,
		.vht160_mcs_rx_highest = 0,
		.vht160_mcs_tx_highest = 0,
	},
	{
		.id = QCA6174_HW_3_0_VERSION,
		.dev_id = QCA6174_2_1_DEVICE_ID,
		.name = "qca6174 hw3.0",
		.patch_load_addr = QCA6174_HW_3_0_PATCH_LOAD_ADDR,
		.uart_pin = 6,
		.otp_exe_param = 0,
		.channel_counters_freq_hz = 88000,
		.max_probe_resp_desc_thres = 0,
		.cal_data_len = 8124,
		.fw = {
			.dir = QCA6174_HW_3_0_FW_DIR,
			.board = QCA6174_HW_3_0_BOARD_DATA_FILE,
			.board_size = QCA6174_BOARD_DATA_SZ,
			.board_ext_size = QCA6174_BOARD_EXT_DATA_SZ,
		},
		.hw_ops = &qca988x_ops,
		.decap_align_bytes = 4,
		.spectral_bin_discard = 0,
		.vht160_mcs_rx_highest = 0,
		.vht160_mcs_tx_highest = 0,
	},
	{
		.id = QCA6174_HW_3_2_VERSION,
		.dev_id = QCA6174_2_1_DEVICE_ID,
		.name = "qca6174 hw3.2",
		.patch_load_addr = QCA6174_HW_3_0_PATCH_LOAD_ADDR,
		.uart_pin = 6,
		.otp_exe_param = 0,
		.channel_counters_freq_hz = 88000,
		.max_probe_resp_desc_thres = 0,
		.cal_data_len = 8124,
		.fw = {
			/* uses same binaries as hw3.0 */
			.dir = QCA6174_HW_3_0_FW_DIR,
			.board = QCA6174_HW_3_0_BOARD_DATA_FILE,
			.board_size = QCA6174_BOARD_DATA_SZ,
			.board_ext_size = QCA6174_BOARD_EXT_DATA_SZ,
		},
		.hw_ops = &qca6174_ops,
		.hw_clk = qca6174_clk,
		.target_cpu_freq = 176000000,
		.decap_align_bytes = 4,
		.spectral_bin_discard = 0,
		.vht160_mcs_rx_highest = 0,
		.vht160_mcs_tx_highest = 0,
	},
	{
		.id = QCA99X0_HW_2_0_DEV_VERSION,
		.dev_id = QCA99X0_2_0_DEVICE_ID,
		.name = "qca99x0 hw2.0",
		.patch_load_addr = QCA99X0_HW_2_0_PATCH_LOAD_ADDR,
		.uart_pin = 7,
		.otp_exe_param = 0x00000700,
		.continuous_frag_desc = true,
		.cck_rate_map_rev2 = true,
		.channel_counters_freq_hz = 150000,
		.max_probe_resp_desc_thres = 24,
		.tx_chain_mask = 0xf,
		.rx_chain_mask = 0xf,
		.max_spatial_stream = 4,
		.cal_data_len = 12064,
		.fw = {
			.dir = QCA99X0_HW_2_0_FW_DIR,
			.board = QCA99X0_HW_2_0_BOARD_DATA_FILE,
			.board_size = QCA99X0_BOARD_DATA_SZ,
			.board_ext_size = QCA99X0_BOARD_EXT_DATA_SZ,
		},
		.sw_decrypt_mcast_mgmt = true,
		.hw_ops = &qca99x0_ops,
		.decap_align_bytes = 1,
		.spectral_bin_discard = 4,
		.vht160_mcs_rx_highest = 0,
		.vht160_mcs_tx_highest = 0,
	},
	{
		.id = QCA9984_HW_1_0_DEV_VERSION,
		.dev_id = QCA9984_1_0_DEVICE_ID,
		.name = "qca9984/qca9994 hw1.0",
		.patch_load_addr = QCA9984_HW_1_0_PATCH_LOAD_ADDR,
		.uart_pin = 7,
		.cc_wraparound_type = ATH10K_HW_CC_WRAP_SHIFTED_EACH,
		.otp_exe_param = 0x00000700,
		.continuous_frag_desc = true,
		.cck_rate_map_rev2 = true,
		.channel_counters_freq_hz = 150000,
		.max_probe_resp_desc_thres = 24,
		.tx_chain_mask = 0xf,
		.rx_chain_mask = 0xf,
		.max_spatial_stream = 4,
		.cal_data_len = 12064,
		.fw = {
			.dir = QCA9984_HW_1_0_FW_DIR,
			.board = QCA9984_HW_1_0_BOARD_DATA_FILE,
			.board_size = QCA99X0_BOARD_DATA_SZ,
			.board_ext_size = QCA99X0_BOARD_EXT_DATA_SZ,
		},
		.sw_decrypt_mcast_mgmt = true,
		.hw_ops = &qca99x0_ops,
		.decap_align_bytes = 1,
		.spectral_bin_discard = 12,

		/* Can do only 2x2 VHT160 or 80+80. 1560Mbps is 4x4 80Mhz
		 * or 2x2 160Mhz, long-guard-interval.
		 */
		.vht160_mcs_rx_highest = 1560,
		.vht160_mcs_tx_highest = 1560,
	},
	{
		.id = QCA9888_HW_2_0_DEV_VERSION,
		.dev_id = QCA9888_2_0_DEVICE_ID,
		.name = "qca9888 hw2.0",
		.patch_load_addr = QCA9888_HW_2_0_PATCH_LOAD_ADDR,
		.uart_pin = 7,
		.cc_wraparound_type = ATH10K_HW_CC_WRAP_SHIFTED_EACH,
		.otp_exe_param = 0x00000700,
		.continuous_frag_desc = true,
		.channel_counters_freq_hz = 150000,
		.max_probe_resp_desc_thres = 24,
		.tx_chain_mask = 3,
		.rx_chain_mask = 3,
		.max_spatial_stream = 2,
		.cal_data_len = 12064,
		.fw = {
			.dir = QCA9888_HW_2_0_FW_DIR,
			.board = QCA9888_HW_2_0_BOARD_DATA_FILE,
			.board_size = QCA99X0_BOARD_DATA_SZ,
			.board_ext_size = QCA99X0_BOARD_EXT_DATA_SZ,
		},
		.sw_decrypt_mcast_mgmt = true,
		.hw_ops = &qca99x0_ops,
		.decap_align_bytes = 1,
		.spectral_bin_discard = 12,

		/* Can do only 1x1 VHT160 or 80+80. 780Mbps is 2x2 80Mhz or
		 * 1x1 160Mhz, long-guard-interval.
		 */
		.vht160_mcs_rx_highest = 780,
		.vht160_mcs_tx_highest = 780,
	},
	{
		.id = QCA9377_HW_1_0_DEV_VERSION,
		.dev_id = QCA9377_1_0_DEVICE_ID,
		.name = "qca9377 hw1.0",
		.patch_load_addr = QCA9377_HW_1_0_PATCH_LOAD_ADDR,
		.uart_pin = 6,
		.otp_exe_param = 0,
		.channel_counters_freq_hz = 88000,
		.max_probe_resp_desc_thres = 0,
		.cal_data_len = 8124,
		.fw = {
			.dir = QCA9377_HW_1_0_FW_DIR,
			.board = QCA9377_HW_1_0_BOARD_DATA_FILE,
			.board_size = QCA9377_BOARD_DATA_SZ,
			.board_ext_size = QCA9377_BOARD_EXT_DATA_SZ,
		},
		.hw_ops = &qca988x_ops,
		.decap_align_bytes = 4,
		.spectral_bin_discard = 0,
		.vht160_mcs_rx_highest = 0,
		.vht160_mcs_tx_highest = 0,
	},
	{
		.id = QCA9377_HW_1_1_DEV_VERSION,
		.dev_id = QCA9377_1_0_DEVICE_ID,
		.name = "qca9377 hw1.1",
		.patch_load_addr = QCA9377_HW_1_0_PATCH_LOAD_ADDR,
		.uart_pin = 6,
		.otp_exe_param = 0,
		.channel_counters_freq_hz = 88000,
		.max_probe_resp_desc_thres = 0,
		.cal_data_len = 8124,
		.fw = {
			.dir = QCA9377_HW_1_0_FW_DIR,
			.board = QCA9377_HW_1_0_BOARD_DATA_FILE,
			.board_size = QCA9377_BOARD_DATA_SZ,
			.board_ext_size = QCA9377_BOARD_EXT_DATA_SZ,
		},
		.hw_ops = &qca6174_ops,
		.hw_clk = qca6174_clk,
		.target_cpu_freq = 176000000,
		.decap_align_bytes = 4,
		.spectral_bin_discard = 0,
		.vht160_mcs_rx_highest = 0,
		.vht160_mcs_tx_highest = 0,
	},
	{
		.id = QCA4019_HW_1_0_DEV_VERSION,
		.dev_id = 0,
		.name = "qca4019 hw1.0",
		.patch_load_addr = QCA4019_HW_1_0_PATCH_LOAD_ADDR,
		.uart_pin = 7,
		.cc_wraparound_type = ATH10K_HW_CC_WRAP_SHIFTED_EACH,
		.otp_exe_param = 0x0010000,
		.continuous_frag_desc = true,
		.cck_rate_map_rev2 = true,
		.channel_counters_freq_hz = 125000,
		.max_probe_resp_desc_thres = 24,
		.tx_chain_mask = 0x3,
		.rx_chain_mask = 0x3,
		.max_spatial_stream = 2,
		.cal_data_len = 12064,
		.fw = {
			.dir = QCA4019_HW_1_0_FW_DIR,
			.board = QCA4019_HW_1_0_BOARD_DATA_FILE,
			.board_size = QCA4019_BOARD_DATA_SZ,
			.board_ext_size = QCA4019_BOARD_EXT_DATA_SZ,
		},
		.sw_decrypt_mcast_mgmt = true,
		.hw_ops = &qca99x0_ops,
		.decap_align_bytes = 1,
		.spectral_bin_discard = 4,
		.vht160_mcs_rx_highest = 0,
		.vht160_mcs_tx_highest = 0,
	},
};

/* 507 */
static zx_status_t ath10k_fetch_fw_file(struct ath10k *ar,
                                        const char *dir,
                                        const char *file,
					struct ath10k_firmware *firmware)
{
        char filename[100];
        int ret;

        if (file == NULL) {
                return ZX_ERR_NOT_FOUND;
	}

        if (dir == NULL) {
                dir = ".";
	}

        snprintf(filename, sizeof(filename), "%s/%s", dir, file);
	ret = load_firmware(ar->zxdev, filename, &firmware->vmo, &firmware->size);
        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot fw request '%s': %s\n",
                   filename, zx_status_get_string(ret));

	if (ret != ZX_OK) {
		return ret;
	}

	firmware->data = malloc(firmware->size);
	if (firmware->data == NULL) {
		return ZX_ERR_NO_MEMORY;
	}

	size_t actual;
	ret = zx_vmo_read(firmware->vmo, firmware->data, 0, firmware->size, &actual);
	if (ret != ZX_OK) {
		return ret;
	}

	if (actual != firmware->size) {
		return ZX_ERR_IO;
	}

        return ZX_OK;
}

/* 974 */
static zx_status_t ath10k_fetch_cal_file(struct ath10k *ar)
{
        char filename[100];
	zx_status_t ret;

        /* pre-cal-<bus>-<id>.bin */
        snprintf(filename, sizeof(filename), "pre-cal-%s-%s.bin",
                 ath10k_bus_str(ar->hif.bus), device_get_name(ar->zxdev));

	ret = ath10k_fetch_fw_file(ar, ATH10K_FW_DIR, filename, &ar->pre_cal_file);
	if (ret == ZX_OK) {
                goto success;
	}

        /* cal-<bus>-<id>.bin */
        snprintf(filename, sizeof(filename), "cal-%s-%s.bin",
                 ath10k_bus_str(ar->hif.bus), device_get_name(ar->zxdev));

	ret = ath10k_fetch_fw_file(ar, ATH10K_FW_DIR, filename, &ar->cal_file);
	if (ret != ZX_OK) {
                /* calibration file is optional, don't print any warnings */
                return ret;
	}

success:
        ath10k_dbg(ar, ATH10K_DBG_BOOT, "found calibration file %s/%s\n",
                   ATH10K_FW_DIR, filename);

        return ZX_OK;
}

/* 1275 */
zx_status_t ath10k_core_fetch_firmware_api_n(struct ath10k *ar, const char *name,
					     struct ath10k_fw_file *fw_file)
{
	size_t magic_len, len, ie_len;
	int ie_id, i, index, bit;
	struct ath10k_fw_ie *hdr;
	const uint8_t *data;
	uint32_t *timestamp, *version;
	zx_status_t ret;

	/* first fetch the firmware file (firmware-*.bin) */
	ret = ath10k_fetch_fw_file(ar, ar->hw_params.fw.dir, name, &fw_file->firmware);
	if (ret != ZX_OK) {
		return ret;
	}

	data = fw_file->firmware.data;
	len = fw_file->firmware.size;

	/* magic also includes the null byte, check that as well */
	magic_len = strlen(ATH10K_FIRMWARE_MAGIC) + 1;

	if (len < magic_len) {
		ath10k_err("firmware file '%s/%s' too small to contain magic: %zu\n",
			   ar->hw_params.fw.dir, name, len);
		ret = ZX_ERR_INVALID_ARGS;
		goto err;
	}

	if (memcmp(data, ATH10K_FIRMWARE_MAGIC, magic_len) != 0) {
		ath10k_err("invalid firmware magic\n");
		ret = ZX_ERR_INVALID_ARGS;
		goto err;
	}

	/* jump over the padding */
	magic_len = ALIGN(magic_len, 4);

	len -= magic_len;
	data += magic_len;

	/* loop elements */
	while (len > sizeof(struct ath10k_fw_ie)) {
		hdr = (struct ath10k_fw_ie *)data;

		ie_id = hdr->id;
		ie_len = hdr->len;

		len -= sizeof(*hdr);
		data += sizeof(*hdr);

		if (len < ie_len) {
			ath10k_err("invalid length for FW IE %d (%zu < %zu)\n",
				   ie_id, len, ie_len);
			ret = ZX_ERR_INVALID_ARGS;
			goto err;
		}

		switch (ie_id) {
		case ATH10K_FW_IE_FW_VERSION:
			if (ie_len > sizeof(fw_file->fw_version) - 1)
				break;

			memcpy(fw_file->fw_version, data, ie_len);
			fw_file->fw_version[ie_len] = '\0';

			ath10k_dbg(ar, ATH10K_DBG_BOOT,
				   "found fw version %s\n",
				    fw_file->fw_version);
			break;
		case ATH10K_FW_IE_TIMESTAMP:
			if (ie_len != sizeof(uint32_t))
				break;

			timestamp = (uint32_t *)data;

			ath10k_dbg(ar, ATH10K_DBG_BOOT, "found fw timestamp %d\n",
				   *timestamp);
			break;
		case ATH10K_FW_IE_FEATURES:
			ath10k_dbg(ar, ATH10K_DBG_BOOT,
				   "found firmware features ie (%zd B)\n",
				   ie_len);

			for (i = 0; i < ATH10K_FW_FEATURE_COUNT; i++) {
				index = i / 8;
				bit = i % 8;

					if ((size_t)index == ie_len)
					break;

				if (data[index] & (1 << bit)) {
					ath10k_dbg(ar, ATH10K_DBG_BOOT,
						   "Enabling feature bit: %i\n",
						   i);
					fw_file->fw_features |= (1 << i);
				}
			}

			ath10k_dbg(ar, ATH10K_DBG_BOOT, "features %" PRIu64 "\n",
				   fw_file->fw_features);
			break;
		case ATH10K_FW_IE_FW_IMAGE:
			ath10k_dbg(ar, ATH10K_DBG_BOOT,
				   "found fw image ie (%zd B)\n",
				   ie_len);

			fw_file->firmware_data = data;
			fw_file->firmware_len = ie_len;

			break;
		case ATH10K_FW_IE_OTP_IMAGE:
			ath10k_dbg(ar, ATH10K_DBG_BOOT,
				   "found otp image ie (%zd B)\n",
				   ie_len);

			fw_file->otp_data = data;
			fw_file->otp_len = ie_len;

			break;
		case ATH10K_FW_IE_WMI_OP_VERSION:
			if (ie_len != sizeof(uint32_t))
				break;

			version = (uint32_t *)data;

			fw_file->wmi_op_version = *version;

			ath10k_dbg(ar, ATH10K_DBG_BOOT, "found fw ie wmi op version %d\n",
				   fw_file->wmi_op_version);
			break;
		case ATH10K_FW_IE_HTT_OP_VERSION:
			if (ie_len != sizeof(uint32_t))
				break;

			version = (uint32_t *)data;

			fw_file->htt_op_version = *version;

			ath10k_dbg(ar, ATH10K_DBG_BOOT, "found fw ie htt op version %d\n",
				   fw_file->htt_op_version);
			break;
		case ATH10K_FW_IE_FW_CODE_SWAP_IMAGE:
			ath10k_dbg(ar, ATH10K_DBG_BOOT,
				   "found fw code swap image ie (%zd B)\n",
				   ie_len);
			fw_file->codeswap_data = data;
			fw_file->codeswap_len = ie_len;
			break;
		default:
			ath10k_warn("Unknown FW IE: %u\n", hdr->id);
			break;
		}

		/* jump over the padding */
		ie_len = ALIGN(ie_len, 4);

		len -= ie_len;
		data += ie_len;
	}

	if (!fw_file->firmware_data ||
	    !fw_file->firmware_len) {
		ath10k_warn("No ATH10K_FW_IE_FW_IMAGE found from '%s/%s', skipping\n",
			    ar->hw_params.fw.dir, name);
		ret = ZX_ERR_NOT_FOUND;
		goto err;
	}

	return ZX_OK;

err:
//	ath10k_core_free_firmware_files(ar);
	return ret;
}

/* 1452 */
static void ath10k_core_get_fw_name(struct ath10k *ar, char *fw_name,
                                    size_t fw_name_len, int fw_api)
{
        switch (ar->hif.bus) {
        case ATH10K_BUS_SDIO:
                snprintf(fw_name, fw_name_len, "%s-%s-%d.bin",
                         ATH10K_FW_FILE_BASE, ath10k_bus_str(ar->hif.bus),
                         fw_api);
                break;
        case ATH10K_BUS_PCI:
        case ATH10K_BUS_AHB:
                snprintf(fw_name, fw_name_len, "%s-%d.bin",
                         ATH10K_FW_FILE_BASE, fw_api);
                break;
        }
}

/* 1469 */
static zx_status_t ath10k_core_fetch_firmware_files(struct ath10k *ar)
{
        zx_status_t ret;
	int i;
        char fw_name[100];

        /* calibration file is optional, don't check for any errors */
        ath10k_fetch_cal_file(ar);

        for (i = ATH10K_FW_API_MAX; i >= ATH10K_FW_API_MIN; i--) {
                ar->fw_api = i;
                ath10k_dbg(ar, ATH10K_DBG_BOOT, "trying fw api %d\n",
                           ar->fw_api);

                ath10k_core_get_fw_name(ar, fw_name, sizeof(fw_name), ar->fw_api);
                ret = ath10k_core_fetch_firmware_api_n(ar, fw_name,
                                                       &ar->normal_mode_fw.fw_file);
                if (ret == ZX_OK)
                        goto success;
        }

        /* we end up here if we couldn't fetch any firmware */

        ath10k_err("Failed to find firmware-N.bin (N between %d and %d) from %s: %d",
                   ATH10K_FW_API_MIN, ATH10K_FW_API_MAX, ar->hw_params.fw.dir,
                   ret);

        return ret;

success:
        ath10k_dbg(ar, ATH10K_DBG_BOOT, "using fw api %d\n", ar->fw_api);

        return 0;
}

/* 1657 */
static zx_status_t ath10k_init_hw_params(struct ath10k *ar)
{
        const struct ath10k_hw_params *hw_params;
        unsigned int i;

        for (i = 0; i < ARRAY_SIZE(ath10k_hw_params_list); i++) {
                hw_params = &ath10k_hw_params_list[i];

                if (hw_params->id == ar->target_version &&
                    hw_params->dev_id == ar->dev_id)
                        break;
        }

        if (i == ARRAY_SIZE(ath10k_hw_params_list)) {
                ath10k_err("Unsupported hardware version: 0x%x\n",
                           ar->target_version);
                return ZX_ERR_INVALID_ARGS;
        }

        ar->hw_params = *hw_params;

        ath10k_info("Hardware name %s version 0x%x\n", ar->hw_params.name,
		    ar->target_version);

        return ZX_OK;
}

/* 2257 */
/* mac80211 manages fw/hw initialization through start/stop hooks. However in
 * order to know what hw capabilities should be advertised to mac80211 it is
 * necessary to load the firmware (and tear it down immediately since start
 * hook will try to init it again) before registering
 */
static zx_status_t ath10k_core_probe_fw(struct ath10k *ar)
{
        struct bmi_target_info target_info;
        int ret = ZX_OK;

        ret = ath10k_hif_power_up(ar);
        if (ret) {
                ath10k_err("could not start pci hif (%d)\n", ret);
                return ret;
        }

        memset(&target_info, 0, sizeof(target_info));
        if (ar->hif.bus == ATH10K_BUS_SDIO)
		// SDIO unsupported
                ZX_DEBUG_ASSERT(0);
        else
                ret = ath10k_bmi_get_target_info(ar, &target_info);
        if (ret) {
                ath10k_err("could not get target info (%d)\n", ret);
                goto err_power_down;
        }

        ar->target_version = target_info.version;

        ret = ath10k_init_hw_params(ar);
        if (ret) {
                ath10k_err("could not get hw params (%d)\n", ret);
                goto err_power_down;
        }

        ret = ath10k_core_fetch_firmware_files(ar);
        if (ret) {
                ath10k_err("could not fetch firmware files (%d)\n", ret);
                goto err_power_down;
        }

#if 0
        BUILD_BUG_ON(sizeof(ar->hw->wiphy->fw_version) !=
                     sizeof(ar->normal_mode_fw.fw_file.fw_version));
        memcpy(ar->hw->wiphy->fw_version, ar->normal_mode_fw.fw_file.fw_version,
               sizeof(ar->hw->wiphy->fw_version));

        ath10k_debug_print_hwfw_info(ar);

        ret = ath10k_core_pre_cal_download(ar);
        if (ret) {
                /* pre calibration data download is not necessary
                 * for all the chipsets. Ignore failures and continue.
                 */
                ath10k_dbg(ar, ATH10K_DBG_BOOT,
                           "could not load pre cal data: %d\n", ret);
        }

        ret = ath10k_core_get_board_id_from_otp(ar);
        if (ret && ret != -EOPNOTSUPP) {
                ath10k_err("failed to get board id from otp: %d\n",
                           ret);
                goto err_free_firmware_files;
        }

        ret = ath10k_core_check_smbios(ar);
        if (ret)
                ath10k_dbg(ar, ATH10K_DBG_BOOT, "bdf variant name not set.\n");

        ret = ath10k_core_fetch_board_file(ar);
        if (ret) {
                ath10k_err("failed to fetch board file: %d\n", ret);
                goto err_free_firmware_files;
        }

        ath10k_debug_print_board_info(ar);

        ret = ath10k_core_init_firmware_features(ar);
        if (ret) {
                ath10k_err("fatal problem with firmware features: %d\n",
                           ret);
                goto err_free_firmware_files;
        }

        ret = ath10k_swap_code_seg_init(ar, &ar->normal_mode_fw.fw_file);
        if (ret) {
                ath10k_err("failed to initialize code swap segment: %d\n",
                           ret);
                goto err_free_firmware_files;
        }

        mutex_lock(&ar->conf_mutex);

        ret = ath10k_core_start(ar, ATH10K_FIRMWARE_MODE_NORMAL,
                                &ar->normal_mode_fw);
        if (ret) {
                ath10k_err("could not init core (%d)\n", ret);
                goto err_unlock;
        }

        ath10k_debug_print_boot_info(ar);
        ath10k_core_stop(ar);

        mutex_unlock(&ar->conf_mutex);

        ath10k_hif_power_down(ar);
        return 0;

err_unlock:
        mutex_unlock(&ar->conf_mutex);

err_free_firmware_files:
        ath10k_core_free_firmware_files(ar);
#endif

err_power_down:
        ath10k_hif_power_down(ar);

        return ret;
}

/* 2376 */
static int ath10k_core_register_work(void *thrd_data) {
	struct ath10k *ar = thrd_data;
        int status;

        /* peer stats are enabled by default */
	ar->dev_flags |= ATH10K_FLAG_PEER_STATS;

        status = ath10k_core_probe_fw(ar);
        if (status) {
                ath10k_err("could not probe fw (%d)\n", status);
                goto err;
        }

#if 0
        status = ath10k_mac_register(ar);
        if (status) {
                ath10k_err("could not register to mac80211 (%d)\n", status);
                goto err_release_fw;
        }

        status = ath10k_debug_register(ar);
        if (status) {
                ath10k_err("unable to initialize debugfs\n");
                goto err_unregister_mac;
        }

        status = ath10k_spectral_create(ar);
        if (status) {
                ath10k_err("failed to initialize spectral\n");
                goto err_debug_destroy;
        }

        status = ath10k_thermal_register(ar);
        if (status) {
                ath10k_err("could not register thermal device: %d\n",
                           status);
                goto err_spectral_destroy;
        }

        set_bit(ATH10K_FLAG_CORE_REGISTERED, &ar->dev_flags);
        return 0;

err_spectral_destroy:
        ath10k_spectral_destroy(ar);
err_debug_destroy:
        ath10k_debug_destroy(ar);
err_unregister_mac:
        ath10k_mac_unregister(ar);
err_release_fw:
        ath10k_core_free_firmware_files(ar);
#endif
err:
        /* TODO: It's probably a good idea to release device from the driver
         * but calling device_release_driver() here will cause a deadlock.
         */
	return 1;
}

/* 2433 */
zx_status_t ath10k_core_register(struct ath10k *ar, uint32_t chip_id)
{
        ar->chip_id = chip_id;
	thrd_create_with_name(&ar->register_work, ath10k_core_register_work, ar,
			      "ath10k_core_register_work");
	thrd_detach(ar->register_work);

        return 0;
}

/* 2481 */
zx_status_t ath10k_core_create(struct ath10k **ar_ptr, size_t priv_size,
				  zx_device_t *dev, enum ath10k_bus bus,
				  enum ath10k_hw_rev hw_rev,
				  const struct ath10k_hif_ops *hif_ops)
{
	struct ath10k* ar;
	zx_status_t ret = ZX_OK;

	ar = ath10k_mac_create(priv_size);
	if (!ar)
		return ZX_ERR_NO_MEMORY;

	ar->ath_common.priv = ar;
	ar->zxdev = dev;
	ar->hw_rev = hw_rev;
	ar->hif.ops = hif_ops;
	ar->hif.bus = bus;

	switch (hw_rev) {
	case ATH10K_HW_QCA988X:
	case ATH10K_HW_QCA9887:
		ar->regs = &qca988x_regs;
		ar->hw_ce_regs = &qcax_ce_regs;
		ar->hw_values = &qca988x_values;
		break;
	case ATH10K_HW_QCA6174:
	case ATH10K_HW_QCA9377:
		ar->regs = &qca6174_regs;
		ar->hw_ce_regs = &qcax_ce_regs;
		ar->hw_values = &qca6174_values;
		break;
	case ATH10K_HW_QCA99X0:
	case ATH10K_HW_QCA9984:
		ar->regs = &qca99x0_regs;
		ar->hw_ce_regs = &qcax_ce_regs;
		ar->hw_values = &qca99x0_values;
		break;
	case ATH10K_HW_QCA9888:
		ar->regs = &qca99x0_regs;
		ar->hw_ce_regs = &qcax_ce_regs;
		ar->hw_values = &qca9888_values;
		break;
	case ATH10K_HW_QCA4019:
		ar->regs = &qca4019_regs;
		ar->hw_ce_regs = &qcax_ce_regs;
		ar->hw_values = &qca4019_values;
		break;
	default:
		ath10k_err("unsupported core hardware revision %d\n",
			   hw_rev);
		ret = ZX_ERR_NOT_SUPPORTED;
		goto err_free_mac;
	}

	mtx_init(&ar->conf_mutex, mtx_plain);
	pthread_spin_init(&ar->data_lock, PTHREAD_PROCESS_PRIVATE);
	pthread_spin_init(&ar->txqs_lock, PTHREAD_PROCESS_PRIVATE);

	list_initialize(&ar->txqs);
	list_initialize(&ar->peers);

	ret = ath10k_debug_create(ar);
	if (ret)
		goto err_free_mac;

	*ar_ptr = ar;
	return ZX_OK;

err_free_mac:
	ath10k_mac_destroy(ar);

	return ret;
}

/* 2589 */
void ath10k_core_destroy(struct ath10k *ar)
{
#if 0
        flush_workqueue(ar->workqueue);
        destroy_workqueue(ar->workqueue);

        flush_workqueue(ar->workqueue_aux);
        destroy_workqueue(ar->workqueue_aux);

        ath10k_debug_destroy(ar);
        ath10k_htt_tx_destroy(&ar->htt);
        ath10k_wmi_free_host_mem(ar);
#endif
        ath10k_mac_destroy(ar);
}

