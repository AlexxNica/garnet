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

#include "linuxisms.h"

#include "mac.h"

#include <stdlib.h>

#include "hif.h"
#include "core.h"
#include "debug.h"

#include <zircon/status.h>

/* Must not be called with conf_mutex held as workers can use that also. */
void ath10k_drain_tx(struct ath10k *ar)
{
	/* TODO - purge the offchan tx queue and mgmt over wmi tx queue */
}

zx_status_t ath10k_start(struct ath10k* ar)
{
//	uint32_t param;
	zx_status_t ret = ZX_OK;

	/*
	 * This makes sense only when restarting hw. It is harmless to call
	 * unconditionally. This is necessary to make sure no HTT/WMI tx
	 * commands will be submitted while restarting.
	 */
	ath10k_drain_tx(ar);
	mtx_lock(&ar->conf_mutex);

	switch (ar->state) {
	case ATH10K_STATE_OFF:
		ar->state = ATH10K_STATE_ON;
		break;
	case ATH10K_STATE_RESTARTING:
		ar->state = ATH10K_STATE_RESTARTED;
		break;
	case ATH10K_STATE_ON:
	case ATH10K_STATE_RESTARTED:
	case ATH10K_STATE_WEDGED:
		WARN_ON(1);
		ret = ZX_ERR_INVALID_ARGS;
		goto err;
	case ATH10K_STATE_UTF:
		ret = ZX_ERR_BAD_STATE;
		goto err;
	}

	ret = ath10k_hif_power_up(ar);
	if (ret != ZX_OK) {
		ath10k_err("Could not init hif: %s\n", zx_status_get_string(ret));
		goto err_off;
	}

#if 0
	ret = ath10k_core_start(ar, ATH10K_FIRMWARE_MODE_NORMAL,
				&ar->normal_mode_fw);
	if (ret) {
		ath10k_err("Could not init core: %d\n", ret);
		goto err_power_down;
	}

	param = ar->wmi.pdev_param->pmf_qos;
	ret = ath10k_wmi_pdev_set_param(ar, param, 1);
	if (ret) {
		ath10k_warn("failed to enable PMF QOS: %d\n", ret);
		goto err_core_stop;
	}

	param = ar->wmi.pdev_param->dynamic_bw;
	ret = ath10k_wmi_pdev_set_param(ar, param, 1);
	if (ret) {
		ath10k_warn("failed to enable dynamic BW: %d\n", ret);
		goto err_core_stop;
	}

	if (test_bit(WMI_SERVICE_ADAPTIVE_OCS, ar->wmi.svc_map)) {
		ret = ath10k_wmi_adaptive_qcs(ar, true);
		if (ret) {
			ath10k_warn("failed to enable adaptive qcs: %d\n",
				    ret);
			goto err_core_stop;
		}
	}

	if (test_bit(WMI_SERVICE_BURST, ar->wmi.svc_map)) {
		param = ar->wmi.pdev_param->burst_enable;
		ret = ath10k_wmi_pdev_set_param(ar, param, 0);
		if (ret) {
			ath10k_warn("failed to disable burst: %d\n", ret);
			goto err_core_stop;
		}
	}

	__ath10k_set_antenna(ar, ar->cfg_tx_chainmask, ar->cfg_rx_chainmask);

	/*
	 * By default FW set ARP frames ac to voice (6). In that case ARP
	 * exchange is not working properly for UAPSD enabled AP. ARP requests
	 * which arrives with access category 0 are processed by network stack
	 * and send back with access category 0, but FW changes access category
	 * to 6. Set ARP frames access category to best effort (0) solves
	 * this problem.
	 */

	param = ar->wmi.pdev_param->arp_ac_override;
	ret = ath10k_wmi_pdev_set_param(ar, param, 0);
	if (ret) {
		ath10k_warn("failed to set arp ac override parameter: %d\n",
			    ret);
		goto err_core_stop;
	}

	if (test_bit(ATH10K_FW_FEATURE_SUPPORTS_ADAPTIVE_CCA,
		     ar->running_fw->fw_file.fw_features)) {
		ret = ath10k_wmi_pdev_enable_adaptive_cca(ar, 1,
							  WMI_CCA_DETECT_LEVEL_AUTO,
							  WMI_CCA_DETECT_MARGIN_AUTO);
		if (ret) {
			ath10k_warn("failed to enable adaptive cca: %d\n",
				    ret);
			goto err_core_stop;
		}
	}

	param = ar->wmi.pdev_param->ani_enable;
	ret = ath10k_wmi_pdev_set_param(ar, param, 1);
	if (ret) {
		ath10k_warn("failed to enable ani by default: %d\n",
			    ret);
		goto err_core_stop;
	}

	ar->ani_enabled = true;

	if (ath10k_peer_stats_enabled(ar)) {
		param = ar->wmi.pdev_param->peer_stats_update_period;
		ret = ath10k_wmi_pdev_set_param(ar, param,
						PEER_DEFAULT_STATS_UPDATE_PERIOD);
		if (ret) {
			ath10k_warn(ar,
				    "failed to set peer stats period : %d\n",
				    ret);
			goto err_core_stop;
		}
	}

	param = ar->wmi.pdev_param->enable_btcoex;
	if (test_bit(WMI_SERVICE_COEX_GPIO, ar->wmi.svc_map) &&
	    test_bit(ATH10K_FW_FEATURE_BTCOEX_PARAM,
		     ar->running_fw->fw_file.fw_features)) {
		ret = ath10k_wmi_pdev_set_param(ar, param, 0);
		if (ret) {
			ath10k_warn(ar,
				    "failed to set btcoex param: %d\n", ret);
			goto err_core_stop;
		}
		clear_bit(ATH10K_FLAG_BTCOEX, &ar->dev_flags);
	}

	ar->num_started_vdevs = 0;
	ath10k_regd_update(ar);

	ath10k_spectral_start(ar);
	ath10k_thermal_set_throttling(ar);
#endif

	mtx_unlock(&ar->conf_mutex);
	return 0;

#if 0
err_core_stop:
	ath10k_core_stop(ar);

err_power_down:
	ath10k_hif_power_down(ar);
#endif

err_off:
	ar->state = ATH10K_STATE_OFF;

err:
	mtx_unlock(&ar->conf_mutex);
	return ret;
}

/* 7718 */
struct ath10k *ath10k_mac_create(size_t priv_size)
{
        struct ath10k* ar;
	void* hif_ctx;

        ar = calloc(1, sizeof(struct ath10k));
        if (!ar) {
                return NULL;
        }

	hif_ctx = calloc(1, priv_size);
	if (!hif_ctx) {
		free(ar);
		return NULL;
	}

	ar->drv_priv = hif_ctx;
	return ar;
}

void ath10k_mac_destroy(struct ath10k *ar)
{
	free(ar->drv_priv);
	free(ar);
}

