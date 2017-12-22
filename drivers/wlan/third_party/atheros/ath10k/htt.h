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

#ifndef _HTT_H_
#define _HTT_H_

#include "htc.h"
#include "hw.h"
#include "hw.h"

/* 1586 */
struct ath10k_htt {
	/* 1593 */
	uint8_t max_num_amsdu;
	uint8_t max_num_ampdu;

	/* 1680 */
	int max_num_pending_tx;
};

/* 1782 */
/* These values are default in most firmware revisions and apparently are a
 * sweet spot performance wise.
 */
#define ATH10K_HTT_MAX_NUM_AMSDU_DEFAULT 3
#define ATH10K_HTT_MAX_NUM_AMPDU_DEFAULT 64

#endif
