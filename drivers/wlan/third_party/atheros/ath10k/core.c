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

#include "core.h"
#include "mac.h"
// #include "htc.h"
#include "hif.h"
// #include "wmi.h"
// #include "bmi.h"
#include "debug.h"
// #include "htt.h"
// #include "testmode.h"
// #include "wmi-ops.h"

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
		ath10k_err(ar, "unsupported core hardware revision %d\n",
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

