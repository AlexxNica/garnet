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
#include "wmi.h"
#include "bmi.h"
#include "debug.h"
#include "htt.h"
// #include "testmode.h"
// #include "wmi-ops.h"
#include "linuxisms.h"

/* 37 */
static unsigned int ath10k_cryptmode_param;
static bool rawmode;

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
        zx_status_t ret;

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
		goto close_vmo;
	}

	if (actual != firmware->size) {
		ret = ZX_ERR_IO;
		goto free_buffer;
	}

        return ZX_OK;

free_buffer:
	free(firmware->data);

close_vmo:
	zx_handle_close(firmware->vmo);

	return ret;
}

/* 532 */
static zx_status_t ath10k_push_board_ext_data(struct ath10k *ar, const void *data,
	                                      size_t data_len)
{
        uint32_t board_data_size = ar->hw_params.fw.board_size;
        uint32_t board_ext_data_size = ar->hw_params.fw.board_ext_size;
        uint32_t board_ext_data_addr;
        zx_status_t ret;

        ret = ath10k_bmi_read32(ar, hi_board_ext_data, &board_ext_data_addr);
        if (ret != ZX_OK) {
                ath10k_err("could not read board ext data addr (%s)\n",
                           zx_status_get_string(ret));
                return ret;
        }

        ath10k_dbg(ar, ATH10K_DBG_BOOT,
                   "boot push board extended data addr 0x%x\n",
                   board_ext_data_addr);

        if (board_ext_data_addr == 0)
                return 0;

        if (data_len != (board_data_size + board_ext_data_size)) {
                ath10k_err("invalid board (ext) data sizes %zu != %d+%d\n",
                           data_len, board_data_size, board_ext_data_size);
                return ZX_ERR_INVALID_ARGS;
        }

        ret = ath10k_bmi_write_memory(ar, board_ext_data_addr,
                                      data + board_data_size,
                                      board_ext_data_size);
        if (ret != ZX_OK) {
                ath10k_err("could not write board ext data (%s)\n",
			   zx_status_get_string(ret));
                return ret;
        }

        ret = ath10k_bmi_write32(ar, hi_board_ext_data_config,
                                 (board_ext_data_size << 16) | 1);
        if (ret != ZX_OK) {
                ath10k_err("could not write board ext data bit (%s)\n",
                           zx_status_get_string(ret));
                return ret;
        }

        return ZX_OK;
}

/* 579 */
static zx_status_t ath10k_download_board_data(struct ath10k *ar, const void *data,
	                                      size_t data_len)
{
        uint32_t board_data_size = ar->hw_params.fw.board_size;
        uint32_t address;
        zx_status_t ret;

        ret = ath10k_push_board_ext_data(ar, data, data_len);
        if (ret != ZX_OK) {
                ath10k_err("could not push board ext data (%s)\n",
			   zx_status_get_string(ret));
                goto exit;
        }

        ret = ath10k_bmi_read32(ar, hi_board_data, &address);
        if (ret != ZX_OK) {
                ath10k_err("could not read board data addr (%s)\n",
			   zx_status_get_string(ret));
                goto exit;
        }

        ret = ath10k_bmi_write_memory(ar, address, data,
                                      min_t(uint32_t, board_data_size,
                                            data_len));
        if (ret != ZX_OK) {
                ath10k_err("could not write board data (%s)\n",
			   zx_status_get_string(ret));
                goto exit;
        }

        ret = ath10k_bmi_write32(ar, hi_board_data_initialized, 1);
        if (ret != ZX_OK) {
                ath10k_err("could not write board data bit (%s)\n",
			   zx_status_get_string(ret));
                goto exit;
        }

exit:
        return ret;
}

/* 646 */
static zx_status_t ath10k_download_cal_file(struct ath10k *ar,
					    const struct ath10k_firmware *file)
{
        zx_status_t ret;

        if (file->vmo == ZX_HANDLE_INVALID)
                return ZX_ERR_BAD_HANDLE;

        ret = ath10k_download_board_data(ar, file->data, file->size);
        if (ret != ZX_OK) {
                ath10k_err("failed to download cal_file data: %s\n",
			   zx_status_get_string(ret));
                return ret;
        }

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot cal file downloaded\n");

        return ZX_OK;
}

/* 722 */
static zx_status_t ath10k_core_get_board_id_from_otp(struct ath10k *ar)
{
        uint32_t result, address;
        uint8_t board_id, chip_id;
        zx_status_t ret;
	int bmi_board_id_param;

        address = ar->hw_params.patch_load_addr;

        if (!ar->normal_mode_fw.fw_file.otp_data ||
            !ar->normal_mode_fw.fw_file.otp_len) {
                ath10k_warn("failed to retrieve board id because of invalid otp\n");
                return ZX_ERR_NOT_FOUND;
        }

        ath10k_dbg(ar, ATH10K_DBG_BOOT,
                   "boot upload otp to 0x%x len %zd for board id\n",
                   address, ar->normal_mode_fw.fw_file.otp_len);

        ret = ath10k_bmi_fast_download(ar, address,
                                       ar->normal_mode_fw.fw_file.otp_data,
                                       ar->normal_mode_fw.fw_file.otp_len);
        if (ret != ZX_OK) {
                ath10k_err("could not write otp for board id check: %s\n",
                           zx_status_get_string(ret));
                return ret;
        }

        if (ar->cal_mode == ATH10K_PRE_CAL_MODE_FILE)
                bmi_board_id_param = BMI_PARAM_GET_FLASH_BOARD_ID;
        else
                bmi_board_id_param = BMI_PARAM_GET_EEPROM_BOARD_ID;

        ret = ath10k_bmi_execute(ar, address, bmi_board_id_param, &result);
        if (ret != ZX_OK) {
                ath10k_err("could not execute otp for board id check: %s\n",
                           zx_status_get_string(ret));
                return ret;
        }

        board_id = MS(result, ATH10K_BMI_BOARD_ID_FROM_OTP);
        chip_id = MS(result, ATH10K_BMI_CHIP_ID_FROM_OTP);

        ath10k_dbg(ar, ATH10K_DBG_BOOT,
                   "boot get otp board id result 0x%08x board_id %d chip_id %d\n",
                   result, board_id, chip_id);

        if ((result & ATH10K_BMI_BOARD_ID_STATUS_MASK) != 0 ||
            (board_id == 0)) {
                ath10k_dbg(ar, ATH10K_DBG_BOOT,
                           "board id does not exist in otp, ignore it\n");
                return ZX_ERR_NOT_SUPPORTED;
        }

        ar->id.bmi_ids_valid = true;
        ar->id.bmi_board_id = board_id;
        ar->id.bmi_chip_id = chip_id;

        return ZX_OK;
}

/* Fuchsia */
static void ath10k_release_firmware(struct ath10k_firmware *fw)
{
	if (fw->vmo != ZX_HANDLE_INVALID) {
		free(fw->data);
		fw->data = NULL;
		zx_handle_close(fw->vmo);
		fw->vmo = ZX_HANDLE_INVALID;
	}
}

/* 940 */
static void ath10k_core_free_board_files(struct ath10k *ar)
{
        ath10k_release_firmware(&ar->normal_mode_fw.board);
        ar->normal_mode_fw.board_data = NULL;
        ar->normal_mode_fw.board_len = 0;
}

/* 950 */
static void ath10k_core_free_firmware_files(struct ath10k *ar)
{
        ath10k_release_firmware(&ar->normal_mode_fw.fw_file.firmware);
        ath10k_release_firmware(&ar->cal_file);
        ath10k_release_firmware(&ar->pre_cal_file);

//	TODO
//	ath10k_swap_code_seg_release(ar, &ar->normal_mode_fw.fw_file);

        ar->normal_mode_fw.fw_file.otp_data = NULL;
        ar->normal_mode_fw.fw_file.otp_len = 0;
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

/* 1001 */
static zx_status_t ath10k_core_fetch_board_data_api_1(struct ath10k *ar)
{
	zx_status_t ret;

        if (!ar->hw_params.fw.board) {
                ath10k_err("failed to find board file fw entry\n");
                return ZX_ERR_INVALID_ARGS;
        }

	ret = ath10k_fetch_fw_file(ar, ar->hw_params.fw.dir, ar->hw_params.fw.board,
				   &ar->normal_mode_fw.board);
	if (ret != ZX_OK) {
		return ret;
	}

        ar->normal_mode_fw.board_data = ar->normal_mode_fw.board.data;
        ar->normal_mode_fw.board_len = ar->normal_mode_fw.board.size;

        return ZX_OK;
}

/* 1020 */
static zx_status_t ath10k_core_parse_bd_ie_board(struct ath10k *ar,
						 const void *buf, size_t buf_len,
						 const char *boardname)
{
	const struct ath10k_fw_ie *hdr;
	bool name_match_found;
	zx_status_t ret;
	int board_ie_id;
	size_t board_ie_len;
	const void *board_ie_data;

	name_match_found = false;

	/* go through ATH10K_BD_IE_BOARD_ elements */
	while (buf_len > sizeof(struct ath10k_fw_ie)) {
		hdr = buf;
		board_ie_id = hdr->id;
		board_ie_len = hdr->len;
		board_ie_data = hdr->data;

		buf_len -= sizeof(*hdr);
		buf += sizeof(*hdr);

		if (buf_len < ALIGN(board_ie_len, 4)) {
			ath10k_err("invalid ATH10K_BD_IE_BOARD length: %zu < %zu\n",
				   buf_len, ALIGN(board_ie_len, 4));
			ret = ZX_ERR_INVALID_ARGS;
			goto out;
		}

		switch (board_ie_id) {
		case ATH10K_BD_IE_BOARD_NAME:
			ath10k_dbg_dump(ar, ATH10K_DBG_BOOT, "board name", "",
					board_ie_data, board_ie_len);

			if (board_ie_len != strlen(boardname))
				break;

			ret = memcmp(board_ie_data, boardname, strlen(boardname));
			if (ret)
				break;

			name_match_found = true;
			ath10k_dbg(ar, ATH10K_DBG_BOOT,
				   "boot found match for name '%s'",
				   boardname);
			break;
		case ATH10K_BD_IE_BOARD_DATA:
			if (!name_match_found)
				/* no match found */
				break;

			ath10k_dbg(ar, ATH10K_DBG_BOOT,
				   "boot found board data for '%s'",
				   boardname);

			ar->normal_mode_fw.board_data = board_ie_data;
			ar->normal_mode_fw.board_len = board_ie_len;

			ret = 0;
			goto out;
		default:
			ath10k_warn("unknown ATH10K_BD_IE_BOARD found: %d\n",
				    board_ie_id);
			break;
		}

		/* jump over the padding */
		board_ie_len = ALIGN(board_ie_len, 4);

		buf_len -= board_ie_len;
		buf += board_ie_len;
	}

	/* no match found */
	ret = ZX_ERR_NOT_FOUND;

out:
	return ret;
}

/* 1100 */
static zx_status_t ath10k_core_fetch_board_data_api_n(struct ath10k *ar,
						      const char *boardname,
						      const char *filename)
{
	size_t len, magic_len, ie_len;
	struct ath10k_fw_ie *hdr;
	const uint8_t *data;
	zx_status_t ret;
	int ie_id;

	ret = ath10k_fetch_fw_file(ar, ar->hw_params.fw.dir, filename, &ar->normal_mode_fw.board);
	if (ret != ZX_OK) {
		return ret;
	}

	data = ar->normal_mode_fw.board.data;
	len = ar->normal_mode_fw.board.size;

	/* magic has extra null byte padded */
	magic_len = strlen(ATH10K_BOARD_MAGIC) + 1;
	if (len < magic_len) {
		ath10k_err("failed to find magic value in %s/%s, file too short: %zu\n",
			   ar->hw_params.fw.dir, filename, len);
		ret = ZX_ERR_INVALID_ARGS;
		goto err;
	}

	if (memcmp(data, ATH10K_BOARD_MAGIC, magic_len)) {
		ath10k_err("found invalid board magic\n");
		ret = ZX_ERR_INVALID_ARGS;
		goto err;
	}

	/* magic is padded to 4 bytes */
	magic_len = ALIGN(magic_len, 4);
	if (len < magic_len) {
		ath10k_err("failed: %s/%s too small to contain board data, len: %zu\n",
			   ar->hw_params.fw.dir, filename, len);
		ret = ZX_ERR_INVALID_ARGS;
		goto err;
	}

	data += magic_len;
	len -= magic_len;

	while (len > sizeof(struct ath10k_fw_ie)) {
		hdr = (struct ath10k_fw_ie *)data;
		ie_id = hdr->id;
		ie_len = hdr->len;

		len -= sizeof(*hdr);
		data = hdr->data;

		if (len < ALIGN(ie_len, 4)) {
			ath10k_err("invalid length for board ie_id %d ie_len %zu len %zu\n",
				   ie_id, ie_len, len);
			ret = ZX_ERR_INVALID_ARGS;
			goto err;
		}

		switch (ie_id) {
		case ATH10K_BD_IE_BOARD:
			ret = ath10k_core_parse_bd_ie_board(ar, data, ie_len,
							    boardname);
			if (ret == ZX_ERR_NOT_FOUND)
				/* no match found, continue */
				break;
			else if (ret != ZX_OK)
				/* there was an error, bail out */
				goto err;

			/* board data found */
			goto out;
		}

		/* jump over the padding */
		ie_len = ALIGN(ie_len, 4);

		len -= ie_len;
		data += ie_len;
	}

out:
	if (!ar->normal_mode_fw.board_data || !ar->normal_mode_fw.board_len) {
		ath10k_err("failed to fetch board data for %s from %s/%s\n",
			   boardname, ar->hw_params.fw.dir, filename);
		ret = ZX_ERR_INVALID_ARGS;
		goto err;
	}

	return ZX_OK;

err:
	ath10k_core_free_board_files(ar);
	return ret;
}

/* 1215 */
static zx_status_t ath10k_core_create_board_name(struct ath10k *ar, char *name,
						 size_t name_len)
{
        if (ar->id.bmi_ids_valid) {
                snprintf(name, name_len,
                         "bus=%s,bmi-chip-id=%d,bmi-board-id=%d",
                         ath10k_bus_str(ar->hif.bus),
                         ar->id.bmi_chip_id,
                         ar->id.bmi_board_id);
                goto out;
        }

        snprintf(name, name_len,
                 "bus=%s,vendor=%04x,device=%04x,subsystem-vendor=%04x,subsystem-device=%04x",
                 ath10k_bus_str(ar->hif.bus),
                 ar->id.vendor, ar->id.device,
                 ar->id.subsystem_vendor, ar->id.subsystem_device);
out:
        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot using board name '%s'\n", name);

        return ZX_OK;
}

/* 1245 */
static zx_status_t ath10k_core_fetch_board_file(struct ath10k *ar)
{
        char boardname[100];
        zx_status_t ret;

        ret = ath10k_core_create_board_name(ar, boardname, sizeof(boardname));
        if (ret != ZX_OK) {
                ath10k_err("failed to create board name: %s", zx_status_get_string(ret));
                return ret;
        }

        ar->bd_api = 2;
        ret = ath10k_core_fetch_board_data_api_n(ar, boardname,
                                                 ATH10K_BOARD_API2_FILE);
        if (ret == ZX_OK)
                goto success;

        ar->bd_api = 1;
        ret = ath10k_core_fetch_board_data_api_1(ar);
        if (ret != ZX_OK) {
                ath10k_err("failed to fetch board-2.bin or board.bin from %s\n",
                           ar->hw_params.fw.dir);
                return ret;
        }

success:
        ath10k_dbg(ar, ATH10K_DBG_BOOT, "using board api %d\n", ar->bd_api);
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
	ath10k_core_free_firmware_files(ar);
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

        return ZX_OK;
}

/* 1503 */
static zx_status_t ath10k_core_pre_cal_download(struct ath10k *ar)
{
        zx_status_t ret;

        ret = ath10k_download_cal_file(ar, &ar->pre_cal_file);
        if (ret == ZX_OK) {
                ar->cal_mode = ATH10K_PRE_CAL_MODE_FILE;
                goto success;
        }

        ath10k_dbg(ar, ATH10K_DBG_BOOT,
                   "boot did not find a pre calibration file, try DT next: %d\n",
                   ret);

	return ZX_ERR_NOT_FOUND;

success:
        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot using calibration mode %s\n",
                   ath10k_cal_mode_str(ar->cal_mode));
        return ZX_OK;
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

/* 1763 */
static zx_status_t ath10k_core_init_firmware_features(struct ath10k *ar)
{
	struct ath10k_fw_file *fw_file = &ar->normal_mode_fw.fw_file;

	if ((fw_file->fw_features & (1ULL << ATH10K_FW_FEATURE_WMI_10_2)) && 
	    !(fw_file->fw_features & (1ULL << ATH10K_FW_FEATURE_WMI_10X))) {
		ath10k_err("feature bits corrupted: 10.2 feature requires 10.x feature to be set as well");
		return ZX_ERR_INVALID_ARGS;
	}

	if (fw_file->wmi_op_version >= ATH10K_FW_WMI_OP_VERSION_MAX) {
		ath10k_err("unsupported WMI OP version (max %d): %d\n",
			   ATH10K_FW_WMI_OP_VERSION_MAX, fw_file->wmi_op_version);
		return ZX_ERR_INVALID_ARGS;
	}

	ar->wmi.rx_decap_mode = ATH10K_HW_TXRX_NATIVE_WIFI;
	switch (ath10k_cryptmode_param) {
	case ATH10K_CRYPT_MODE_HW:
		ar->dev_flags &= ~ATH10K_FLAG_RAW_MODE;
		ar->dev_flags &= ~ATH10K_FLAG_HW_CRYPTO_DISABLED;
		break;
	case ATH10K_CRYPT_MODE_SW:
		if (!(fw_file->fw_features & (1ULL << ATH10K_FW_FEATURE_RAW_MODE_SUPPORT))) {
			ath10k_err("cryptmode > 0 requires raw mode support from firmware");
			return ZX_ERR_INVALID_ARGS;
		}

		ar->dev_flags |= ATH10K_FLAG_RAW_MODE;
		ar->dev_flags |= ATH10K_FLAG_HW_CRYPTO_DISABLED;
		break;
	default:
		ath10k_info("invalid cryptmode: %d\n", ath10k_cryptmode_param);
		return ZX_ERR_INVALID_ARGS;
	}

	ar->htt.max_num_amsdu = ATH10K_HTT_MAX_NUM_AMSDU_DEFAULT;
	ar->htt.max_num_ampdu = ATH10K_HTT_MAX_NUM_AMPDU_DEFAULT;

	if (rawmode) {
		if (!(fw_file->fw_features & (1ULL << ATH10K_FW_FEATURE_RAW_MODE_SUPPORT))) {
			ath10k_err("rawmode = 1 requires support from firmware");
			return ZX_ERR_INVALID_ARGS;
		}
		ar->dev_flags |= ATH10K_FLAG_RAW_MODE;
	}

	if (ar->dev_flags & ATH10K_FLAG_RAW_MODE) {
		ar->wmi.rx_decap_mode = ATH10K_HW_TXRX_RAW;

		/* Workaround:
		 *
		 * Firmware A-MSDU aggregation breaks with RAW Tx encap mode
		 * and causes enormous performance issues (malformed frames,
		 * etc).
		 *
		 * Disabling A-MSDU makes RAW mode stable with heavy traffic
		 * albeit a bit slower compared to regular operation.
		 */
		ar->htt.max_num_amsdu = 1;
	}

	/* Backwards compatibility for firmwares without
	 * ATH10K_FW_IE_WMI_OP_VERSION.
	 */
	if (fw_file->wmi_op_version == ATH10K_FW_WMI_OP_VERSION_UNSET) {
		if (fw_file->fw_features & (1ULL << ATH10K_FW_FEATURE_WMI_10X)) {
			if (fw_file->fw_features & (1ULL << ATH10K_FW_FEATURE_WMI_10_2)) {
				fw_file->wmi_op_version = ATH10K_FW_WMI_OP_VERSION_10_2;
			} else {
				fw_file->wmi_op_version = ATH10K_FW_WMI_OP_VERSION_10_1;
			}
		} else {
			fw_file->wmi_op_version = ATH10K_FW_WMI_OP_VERSION_MAIN;
		}
	}

	switch (fw_file->wmi_op_version) {
	case ATH10K_FW_WMI_OP_VERSION_MAIN:
		ar->max_num_peers = TARGET_NUM_PEERS;
		ar->max_num_stations = TARGET_NUM_STATIONS;
		ar->max_num_vdevs = TARGET_NUM_VDEVS;
		ar->htt.max_num_pending_tx = TARGET_NUM_MSDU_DESC;
		ar->fw_stats_req_mask = WMI_STAT_PDEV | WMI_STAT_VDEV |
			WMI_STAT_PEER;
		ar->max_spatial_stream = WMI_MAX_SPATIAL_STREAM;
		break;
	case ATH10K_FW_WMI_OP_VERSION_10_1:
	case ATH10K_FW_WMI_OP_VERSION_10_2:
	case ATH10K_FW_WMI_OP_VERSION_10_2_4:
		if (ath10k_peer_stats_enabled(ar)) {
			ar->max_num_peers = TARGET_10X_TX_STATS_NUM_PEERS;
			ar->max_num_stations = TARGET_10X_TX_STATS_NUM_STATIONS;
		} else {
			ar->max_num_peers = TARGET_10X_NUM_PEERS;
			ar->max_num_stations = TARGET_10X_NUM_STATIONS;
		}
		ar->max_num_vdevs = TARGET_10X_NUM_VDEVS;
		ar->htt.max_num_pending_tx = TARGET_10X_NUM_MSDU_DESC;
		ar->fw_stats_req_mask = WMI_STAT_PEER;
		ar->max_spatial_stream = WMI_MAX_SPATIAL_STREAM;
		break;
	case ATH10K_FW_WMI_OP_VERSION_TLV:
		ar->max_num_peers = TARGET_TLV_NUM_PEERS;
		ar->max_num_stations = TARGET_TLV_NUM_STATIONS;
		ar->max_num_vdevs = TARGET_TLV_NUM_VDEVS;
		ar->max_num_tdls_vdevs = TARGET_TLV_NUM_TDLS_VDEVS;
		ar->htt.max_num_pending_tx = TARGET_TLV_NUM_MSDU_DESC;
		ar->wow.max_num_patterns = TARGET_TLV_NUM_WOW_PATTERNS;
		ar->fw_stats_req_mask = WMI_STAT_PDEV | WMI_STAT_VDEV |
			WMI_STAT_PEER;
		ar->max_spatial_stream = WMI_MAX_SPATIAL_STREAM;
		break;
	case ATH10K_FW_WMI_OP_VERSION_10_4:
		ar->max_num_peers = TARGET_10_4_NUM_PEERS;
		ar->max_num_stations = TARGET_10_4_NUM_STATIONS;
		ar->num_active_peers = TARGET_10_4_ACTIVE_PEERS;
		ar->max_num_vdevs = TARGET_10_4_NUM_VDEVS;
		ar->num_tids = TARGET_10_4_TGT_NUM_TIDS;
		ar->fw_stats_req_mask = WMI_10_4_STAT_PEER |
					WMI_10_4_STAT_PEER_EXTD;
		ar->max_spatial_stream = ar->hw_params.max_spatial_stream;

		if (fw_file->fw_features & (1ULL << ATH10K_FW_FEATURE_PEER_FLOW_CONTROL)) {
			ar->htt.max_num_pending_tx = TARGET_10_4_NUM_MSDU_DESC_PFC;
		} else {
			ar->htt.max_num_pending_tx = TARGET_10_4_NUM_MSDU_DESC;
		}
		break;
	case ATH10K_FW_WMI_OP_VERSION_UNSET:
	case ATH10K_FW_WMI_OP_VERSION_MAX:
		WARN_ON(1);
		return ZX_ERR_INVALID_ARGS;
	}

	/* Backwards compatibility for firmwares without
	 * ATH10K_FW_IE_HTT_OP_VERSION.
	 */
	if (fw_file->htt_op_version == ATH10K_FW_HTT_OP_VERSION_UNSET) {
		switch (fw_file->wmi_op_version) {
		case ATH10K_FW_WMI_OP_VERSION_MAIN:
			fw_file->htt_op_version = ATH10K_FW_HTT_OP_VERSION_MAIN;
			break;
		case ATH10K_FW_WMI_OP_VERSION_10_1:
		case ATH10K_FW_WMI_OP_VERSION_10_2:
		case ATH10K_FW_WMI_OP_VERSION_10_2_4:
			fw_file->htt_op_version = ATH10K_FW_HTT_OP_VERSION_10_1;
			break;
		case ATH10K_FW_WMI_OP_VERSION_TLV:
			fw_file->htt_op_version = ATH10K_FW_HTT_OP_VERSION_TLV;
			break;
		case ATH10K_FW_WMI_OP_VERSION_10_4:
		case ATH10K_FW_WMI_OP_VERSION_UNSET:
		case ATH10K_FW_WMI_OP_VERSION_MAX:
			ath10k_err("htt op version not found from fw meta data");
			return ZX_ERR_INVALID_ARGS;
		}
	}

	return ZX_OK;
}

/* 1977 */
zx_status_t ath10k_core_start(struct ath10k *ar, enum ath10k_firmware_mode mode,
			      const struct ath10k_fw_components *fw)
{
#if 0
	zx_status_t status;
	u32 val;

	lockdep_assert_held(&ar->conf_mutex);

	clear_bit(ATH10K_FLAG_CRASH_FLUSH, &ar->dev_flags);

	ar->running_fw = fw;

	ath10k_bmi_start(ar);

	if (ath10k_init_configure_target(ar)) {
		status = -EINVAL;
		goto err;
	}

	status = ath10k_download_cal_data(ar);
	if (status)
		goto err;

	/* Some of of qca988x solutions are having global reset issue
	 * during target initialization. Bypassing PLL setting before
	 * downloading firmware and letting the SoC run on REF_CLK is
	 * fixing the problem. Corresponding firmware change is also needed
	 * to set the clock source once the target is initialized.
	 */
	if (test_bit(ATH10K_FW_FEATURE_SUPPORTS_SKIP_CLOCK_INIT,
		     ar->running_fw->fw_file.fw_features)) {
		status = ath10k_bmi_write32(ar, hi_skip_clock_init, 1);
		if (status) {
			ath10k_err(ar, "could not write to skip_clock_init: %d\n",
				   status);
			goto err;
		}
	}

	status = ath10k_download_fw(ar);
	if (status)
		goto err;

	status = ath10k_init_uart(ar);
	if (status)
		goto err;

	if (ar->hif.bus == ATH10K_BUS_SDIO)
		ath10k_init_sdio(ar);

	ar->htc.htc_ops.target_send_suspend_complete =
		ath10k_send_suspend_complete;

	status = ath10k_htc_init(ar);
	if (status) {
		ath10k_err(ar, "could not init HTC (%d)\n", status);
		goto err;
	}

	status = ath10k_bmi_done(ar);
	if (status)
		goto err;

	status = ath10k_wmi_attach(ar);
	if (status) {
		ath10k_err(ar, "WMI attach failed: %d\n", status);
		goto err;
	}

	status = ath10k_htt_init(ar);
	if (status) {
		ath10k_err(ar, "failed to init htt: %d\n", status);
		goto err_wmi_detach;
	}

	status = ath10k_htt_tx_start(&ar->htt);
	if (status) {
		ath10k_err(ar, "failed to alloc htt tx: %d\n", status);
		goto err_wmi_detach;
	}

	status = ath10k_htt_rx_alloc(&ar->htt);
	if (status) {
		ath10k_err(ar, "failed to alloc htt rx: %d\n", status);
		goto err_htt_tx_detach;
	}

	status = ath10k_hif_start(ar);
	if (status) {
		ath10k_err(ar, "could not start HIF: %d\n", status);
		goto err_htt_rx_detach;
	}

	status = ath10k_htc_wait_target(&ar->htc);
	if (status) {
		ath10k_err(ar, "failed to connect to HTC: %d\n", status);
		goto err_hif_stop;
	}

	if (mode == ATH10K_FIRMWARE_MODE_NORMAL) {
		status = ath10k_htt_connect(&ar->htt);
		if (status) {
			ath10k_err(ar, "failed to connect htt (%d)\n", status);
			goto err_hif_stop;
		}
	}

	status = ath10k_wmi_connect(ar);
	if (status) {
		ath10k_err(ar, "could not connect wmi: %d\n", status);
		goto err_hif_stop;
	}

	status = ath10k_htc_start(&ar->htc);
	if (status) {
		ath10k_err(ar, "failed to start htc: %d\n", status);
		goto err_hif_stop;
	}

	if (mode == ATH10K_FIRMWARE_MODE_NORMAL) {
		status = ath10k_wmi_wait_for_service_ready(ar);
		if (status) {
			ath10k_warn(ar, "wmi service ready event not received");
			goto err_hif_stop;
		}
	}

	ath10k_dbg(ar, ATH10K_DBG_BOOT, "firmware %s booted\n",
		   ar->hw->wiphy->fw_version);

	if (test_bit(WMI_SERVICE_EXT_RES_CFG_SUPPORT, ar->wmi.svc_map) &&
	    mode == ATH10K_FIRMWARE_MODE_NORMAL) {
		val = 0;
		if (ath10k_peer_stats_enabled(ar))
			val = WMI_10_4_PEER_STATS;

		if (test_bit(WMI_SERVICE_BSS_CHANNEL_INFO_64, ar->wmi.svc_map))
			val |= WMI_10_4_BSS_CHANNEL_INFO_64;

		/* 10.4 firmware supports BT-Coex without reloading firmware
		 * via pdev param. To support Bluetooth coexistence pdev param,
		 * WMI_COEX_GPIO_SUPPORT of extended resource config should be
		 * enabled always.
		 */
		if (test_bit(WMI_SERVICE_COEX_GPIO, ar->wmi.svc_map) &&
		    test_bit(ATH10K_FW_FEATURE_BTCOEX_PARAM,
			     ar->running_fw->fw_file.fw_features))
			val |= WMI_10_4_COEX_GPIO_SUPPORT;

		status = ath10k_mac_ext_resource_config(ar, val);
		if (status) {
			ath10k_err(ar,
				   "failed to send ext resource cfg command : %d\n",
				   status);
			goto err_hif_stop;
		}
	}

	status = ath10k_wmi_cmd_init(ar);
	if (status) {
		ath10k_err(ar, "could not send WMI init command (%d)\n",
			   status);
		goto err_hif_stop;
	}

	status = ath10k_wmi_wait_for_unified_ready(ar);
	if (status) {
		ath10k_err(ar, "wmi unified ready event not received\n");
		goto err_hif_stop;
	}

	/* Some firmware revisions do not properly set up hardware rx filter
	 * registers.
	 *
	 * A known example from QCA9880 and 10.2.4 is that MAC_PCU_ADDR1_MASK
	 * is filled with 0s instead of 1s allowing HW to respond with ACKs to
	 * any frames that matches MAC_PCU_RX_FILTER which is also
	 * misconfigured to accept anything.
	 *
	 * The ADDR1 is programmed using internal firmware structure field and
	 * can't be (easily/sanely) reached from the driver explicitly. It is
	 * possible to implicitly make it correct by creating a dummy vdev and
	 * then deleting it.
	 */
	if (mode == ATH10K_FIRMWARE_MODE_NORMAL) {
		status = ath10k_core_reset_rx_filter(ar);
		if (status) {
			ath10k_err(ar,
				   "failed to reset rx filter: %d\n", status);
			goto err_hif_stop;
		}
	}

	/* If firmware indicates Full Rx Reorder support it must be used in a
	 * slightly different manner. Let HTT code know.
	 */
	ar->htt.rx_ring.in_ord_rx = !!(test_bit(WMI_SERVICE_RX_FULL_REORDER,
						ar->wmi.svc_map));

	status = ath10k_htt_rx_ring_refill(ar);
	if (status) {
		ath10k_err(ar, "failed to refill htt rx ring: %d\n", status);
		goto err_hif_stop;
	}

	if (ar->max_num_vdevs >= 64)
		ar->free_vdev_map = 0xFFFFFFFFFFFFFFFFLL;
	else
		ar->free_vdev_map = (1LL << ar->max_num_vdevs) - 1;

	INIT_LIST_HEAD(&ar->arvifs);

	/* we don't care about HTT in UTF mode */
	if (mode == ATH10K_FIRMWARE_MODE_NORMAL) {
		status = ath10k_htt_setup(&ar->htt);
		if (status) {
			ath10k_err(ar, "failed to setup htt: %d\n", status);
			goto err_hif_stop;
		}
	}

	status = ath10k_debug_start(ar);
	if (status)
		goto err_hif_stop;

	return 0;

err_hif_stop:
	ath10k_hif_stop(ar);
err_htt_rx_detach:
	ath10k_htt_rx_free(&ar->htt);
err_htt_tx_detach:
	ath10k_htt_tx_free(&ar->htt);
err_wmi_detach:
	ath10k_wmi_detach(ar);
err:
	return status;
#endif
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
        zx_status_t ret;

        ret = ath10k_hif_power_up(ar);
        if (ret != ZX_OK) {
                ath10k_err("could not start pci hif (%s)\n",
			   zx_status_get_string(ret));
                return ret;
        }

        memset(&target_info, 0, sizeof(target_info));
        if (ar->hif.bus == ATH10K_BUS_SDIO)
		// SDIO unsupported
                ZX_DEBUG_ASSERT(0);
        else
                ret = ath10k_bmi_get_target_info(ar, &target_info);
        if (ret != ZX_OK) {
                ath10k_err("could not get target info (%s)\n", zx_status_get_string(ret));
                goto err_power_down;
        }

        ar->target_version = target_info.version;

        ret = ath10k_init_hw_params(ar);
        if (ret != ZX_OK) {
                ath10k_err("could not get hw params (%s)\n", zx_status_get_string(ret));
                goto err_power_down;
        }

        ret = ath10k_core_fetch_firmware_files(ar);
        if (ret != ZX_OK) {
                ath10k_err("could not fetch firmware files (%s)\n", zx_status_get_string(ret));
                goto err_power_down;
        }

//	TODO
//	ath10k_debug_print_hwfw_info(ar);

        ret = ath10k_core_pre_cal_download(ar);
        if (ret != ZX_OK) {
                /* pre calibration data download is not necessary
                 * for all the chipsets. Ignore failures and continue.
                 */
                ath10k_dbg(ar, ATH10K_DBG_BOOT,
                           "could not load pre cal data: %s\n", zx_status_get_string(ret));
        }

        ret = ath10k_core_get_board_id_from_otp(ar);
        if (ret != ZX_OK && ret != ZX_ERR_NOT_SUPPORTED) {
                ath10k_err("failed to get board id from otp: %s\n",
                           zx_status_get_string(ret));
                goto err_free_firmware_files;
        }

#if 0
        ret = ath10k_core_check_smbios(ar);
        if (ret != ZX_OK) {
                ath10k_dbg(ar, ATH10K_DBG_BOOT, "bdf variant name not set.\n");
	}
#endif

        ret = ath10k_core_fetch_board_file(ar);
        if (ret != ZX_OK) {
                ath10k_err("failed to fetch board file: %s\n", zx_status_get_string(ret));
                goto err_free_firmware_files;
        }

//	TODO
//      ath10k_debug_print_board_info(ar);

        ret = ath10k_core_init_firmware_features(ar);
        if (ret != ZX_OK) {
                ath10k_err("fatal problem with firmware features: %s\n",
                           zx_status_get_string(ret));
                goto err_free_firmware_files;
        }

#if 0
        ret = ath10k_swap_code_seg_init(ar, &ar->normal_mode_fw.fw_file);
        if (ret != ZX_OK) {
                ath10k_err("failed to initialize code swap segment: %d\n",
                           ret);
                goto err_free_firmware_files;
        }
#endif

        mtx_lock(&ar->conf_mutex);

        ret = ath10k_core_start(ar, ATH10K_FIRMWARE_MODE_NORMAL,
                                &ar->normal_mode_fw);
        if (ret != ZX_OK) {
                ath10k_err("could not init core (%d)\n", ret);
                goto err_unlock;
        }

//	TODO
//      ath10k_debug_print_boot_info(ar);

//        ath10k_core_stop(ar);

        mtx_unlock(&ar->conf_mutex);

        ath10k_hif_power_down(ar);
        return 0;

err_unlock:
        mtx_unlock(&ar->conf_mutex);

err_free_firmware_files:
        ath10k_core_free_firmware_files(ar);

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

	ar->dev_flags |= ATH10K_FLAG_CORE_REGISTERED;
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

