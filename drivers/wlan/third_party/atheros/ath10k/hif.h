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

#ifndef _HIF_H_
#define _HIF_H_

#include "core.h"
#include "debug.h"

struct ath10k_hif_ops {
	/* 44 */
        /*
         * API to handle HIF-specific BMI message exchanges, this API is
         * synchronous and only allowed to be called from a context that
         * can block (sleep)
         */
        zx_status_t (*exchange_bmi_msg)(struct ath10k *ar,
					void *request, uint32_t request_len,
					void *response, uint32_t *response_len);

	/* 82 */
	/* Power up the device and enter BMI transfer mode for FW download */
	zx_status_t (*power_up)(struct ath10k *ar);

	/* 85 */
        /* Power down the device and free up resources. stop() must be called
         * before this if start() was called earlier
         */
        void (*power_down)(struct ath10k *ar);
};

/* 120 */
static inline zx_status_t ath10k_hif_exchange_bmi_msg(struct ath10k *ar,
	                                              void *request, uint32_t request_len,
	                                              void *response, uint32_t *response_len)
{
        return ar->hif.ops->exchange_bmi_msg(ar, request, request_len,
                                             response, response_len);
}

/* 164 */
static inline zx_status_t ath10k_hif_power_up(struct ath10k *ar)
{
	return ar->hif.ops->power_up(ar);
}

/* 169 */
static inline void ath10k_hif_power_down(struct ath10k *ar)
{
        ar->hif.ops->power_down(ar);
}

#endif /* _HIF_H_ */
