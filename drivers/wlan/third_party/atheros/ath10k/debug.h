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

#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <ddk/debug.h>

/* Fuchsia */
// #define ath10k_info(fmt, ...) zxlogf(INFO, "ath10k: " fmt, ##__VA_ARGS__)
#define ath10k_info(fmt, ...) zxlogf(ERROR, "ath10k: " fmt, ##__VA_ARGS__)
#define ath10k_err(fmt, ...) zxlogf(ERROR, "ath10k: " fmt, ##__VA_ARGS__)
/* Fuchsia has no level between ERROR and INFO */
#define ath10k_warn(fmt, ...) zxlogf(ERROR, "ath10k: " fmt, ##__VA_ARGS__)

/* 24 */
enum ath10k_debug_mask {
        ATH10K_DBG_PCI          = 0x00000001,
        ATH10K_DBG_WMI          = 0x00000002,
        ATH10K_DBG_HTC          = 0x00000004,
        ATH10K_DBG_HTT          = 0x00000008,
        ATH10K_DBG_MAC          = 0x00000010,
        ATH10K_DBG_BOOT         = 0x00000020,
        ATH10K_DBG_PCI_DUMP     = 0x00000040,
        ATH10K_DBG_HTT_DUMP     = 0x00000080,
        ATH10K_DBG_MGMT         = 0x00000100,
        ATH10K_DBG_DATA         = 0x00000200,
        ATH10K_DBG_BMI          = 0x00000400,
        ATH10K_DBG_REGULATORY   = 0x00000800,
        ATH10K_DBG_TESTMODE     = 0x00001000,
        ATH10K_DBG_WMI_PRINT    = 0x00002000,
        ATH10K_DBG_PCI_PS       = 0x00004000,
        ATH10K_DBG_AHB          = 0x00008000,
        ATH10K_DBG_SDIO         = 0x00010000,
        ATH10K_DBG_SDIO_DUMP    = 0x00020000,
        ATH10K_DBG_ANY          = 0xffffffff,
};

/* 75 */
#ifdef CONFIG_ATH10K_DEBUGFS
/* 78 */
int ath10k_debug_create(struct ath10k *ar);
/* 113 */
#else
/* 124 */
static inline int ath10k_debug_create(struct ath10k *ar)
{
        return 0;
}
/* 185 */
#endif /* CONFIG_ATH10K_DEBUGFS */

/* 202 */
#ifdef CONFIG_ATH10K_DEBUG
__printf(3, 4) void ath10k_dbg(struct ath10k *ar,
                               enum ath10k_debug_mask mask,
                               const char *fmt, ...);
void ath10k_dbg_dump(struct ath10k *ar,
                     enum ath10k_debug_mask mask,
                     const char *msg, const char *prefix,
                     const void *buf, size_t len);
#else /* CONFIG_ATH10K_DEBUG */

static inline int ath10k_dbg(struct ath10k *ar,
                             enum ath10k_debug_mask dbg_mask,
                             const char *fmt, ...)
{
        return 0;
}

static inline void ath10k_dbg_dump(struct ath10k *ar,
                                   enum ath10k_debug_mask mask,
                                   const char *msg, const char *prefix,
                                   const void *buf, size_t len)
{
}
#endif /* CONFIG_ATH10K_DEBUG */

#endif /* _DEBUG_H_ */
