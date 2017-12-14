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
#include <threads.h>
#include <pthread.h>

#include <ddk/device.h>

#include "hw.h"
#include "targaddrs.h"
#include "../ath.h"

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

/* 149 */
static inline uint32_t host_interest_item_address(uint32_t item_offset)
{
        return QCA988X_HOST_INTEREST_ADDRESS + item_offset;
}

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

/* 758 */
struct ath10k {
	struct ath_common ath_common;

	/* Fuchsia */
	zx_device_t* zxdev;
	thrd_t init_thread;

	/* 765 */
	enum ath10k_hw_rev hw_rev;

	/* 766 */
	uint16_t dev_id;
	uint32_t chip_id;

	/* 789 */
        struct {
                enum ath10k_bus bus;
                const struct ath10k_hif_ops *ops;
        } hif;

	/* 796 */
        const struct ath10k_hw_regs *regs;
        const struct ath10k_hw_ce_regs *hw_ce_regs;
        const struct ath10k_hw_values *hw_values;

	/* 817 */
        struct {
                uint32_t vendor;
                uint32_t device;
        } id;

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

	/* 923 */
	enum ath10k_state state;

	/* 968 */
        struct {
                /* protected by data_lock */
                uint32_t fw_crash_counter;
                uint32_t fw_warm_reset_counter;
                uint32_t fw_cold_reset_counter;
        } stats;

	/* 996 */
        /* must be last */
        void* drv_priv;
};

/* 1009 */
zx_status_t ath10k_core_create(struct ath10k **ar_ptr, size_t priv_size,
				  zx_device_t *dev, enum ath10k_bus bus,
                                  enum ath10k_hw_rev hw_rev,
                                  const struct ath10k_hif_ops *hif_ops);
void ath10k_core_destroy(struct ath10k *ar);

#endif /* _CORE_H_ */
