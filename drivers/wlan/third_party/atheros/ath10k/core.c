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

#if 0
        ar->target_version = target_info.version;
        ar->hw->wiphy->hw_version = target_info.version;

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

