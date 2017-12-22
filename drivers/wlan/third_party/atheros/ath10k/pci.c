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
#include "debug.h"

#include "targaddrs.h"
#include "bmi.h"

#include "hif.h"
#include "htc.h"

#include "ce.h"
#include "pci.h"

#include "linuxisms.h"

#include <stdio.h>
#include <stdlib.h>
#define _ALL_SOURCE
#include <threads.h>

#include <ddk/device.h>
#include <ddk/driver.h>
#include <ddk/protocol/ethernet.h>
#include <ddk/protocol/pci.h>
#include <ddk/protocol/wlan.h>
#include <ddk/binding.h>
#include <zircon/status.h>
#include <zircon/types.h>

/* 36 */
enum ath10k_pci_reset_mode {
        ATH10K_PCI_RESET_AUTO = 0,
        ATH10K_PCI_RESET_WARM_ONLY = 1,
};

/* 41 */
static unsigned int ath10k_pci_irq_mode = ATH10K_PCI_IRQ_AUTO;
static unsigned int ath10k_pci_reset_mode = ATH10K_PCI_RESET_AUTO;

/* 50 */
/* how long wait to wait for target to initialise, in ms */
#define ATH10K_PCI_TARGET_WAIT 3000
#define ATH10K_PCI_NUM_WARM_RESET_ATTEMPTS 3

/* 66 */
static const struct ath10k_pci_supp_chip ath10k_pci_supp_chips[] = {
        /* QCA988X pre 2.0 chips are not supported because they need some nasty
         * hacks. ath10k doesn't have them and these devices crash horribly
         * because of that.
         */
        { QCA988X_2_0_DEVICE_ID, QCA988X_HW_2_0_CHIP_ID_REV },

        { QCA6164_2_1_DEVICE_ID, QCA6174_HW_2_1_CHIP_ID_REV },
        { QCA6164_2_1_DEVICE_ID, QCA6174_HW_2_2_CHIP_ID_REV },
        { QCA6164_2_1_DEVICE_ID, QCA6174_HW_3_0_CHIP_ID_REV },
        { QCA6164_2_1_DEVICE_ID, QCA6174_HW_3_1_CHIP_ID_REV },
        { QCA6164_2_1_DEVICE_ID, QCA6174_HW_3_2_CHIP_ID_REV },

        { QCA6174_2_1_DEVICE_ID, QCA6174_HW_2_1_CHIP_ID_REV },
        { QCA6174_2_1_DEVICE_ID, QCA6174_HW_2_2_CHIP_ID_REV },
        { QCA6174_2_1_DEVICE_ID, QCA6174_HW_3_0_CHIP_ID_REV },
        { QCA6174_2_1_DEVICE_ID, QCA6174_HW_3_1_CHIP_ID_REV },
        { QCA6174_2_1_DEVICE_ID, QCA6174_HW_3_2_CHIP_ID_REV },

        { QCA99X0_2_0_DEVICE_ID, QCA99X0_HW_2_0_CHIP_ID_REV },

        { QCA9984_1_0_DEVICE_ID, QCA9984_HW_1_0_CHIP_ID_REV },

        { QCA9888_2_0_DEVICE_ID, QCA9888_HW_2_0_CHIP_ID_REV },

        { QCA9377_1_0_DEVICE_ID, QCA9377_HW_1_0_CHIP_ID_REV },
        { QCA9377_1_0_DEVICE_ID, QCA9377_HW_1_1_CHIP_ID_REV },

        { QCA9887_1_0_DEVICE_ID, QCA9887_HW_1_0_CHIP_ID_REV },
};

/* 98 */
static int ath10k_pci_cold_reset(struct ath10k *ar);

/* 100 */
static zx_status_t ath10k_pci_init_irq(struct ath10k *ar);
static void ath10k_pci_deinit_irq(struct ath10k *ar);
static zx_status_t ath10k_pci_request_irq(struct ath10k *ar);
static void ath10k_pci_free_irq(struct ath10k *ar);
static zx_status_t ath10k_pci_bmi_wait(struct ath10k *ar,
				       struct ath10k_ce_pipe *tx_pipe,
				       struct ath10k_ce_pipe *rx_pipe,
				       struct bmi_xfer *xfer);
static int ath10k_pci_qca99x0_chip_reset(struct ath10k *ar);
static void ath10k_pci_htc_tx_cb(struct ath10k_ce_pipe *ce_state);
static void ath10k_pci_htc_rx_cb(struct ath10k_ce_pipe *ce_state);
static void ath10k_pci_htt_tx_cb(struct ath10k_ce_pipe *ce_state);
static void ath10k_pci_htt_rx_cb(struct ath10k_ce_pipe *ce_state);
static void ath10k_pci_htt_htc_rx_cb(struct ath10k_ce_pipe *ce_state);
static void ath10k_pci_pktlog_rx_cb(struct ath10k_ce_pipe *ce_state);

/* 113 */
static void ath10k_pci_htt_htc_rx_cb(struct ath10k_ce_pipe *ce_state);

/* 116 */
static struct ce_attr host_ce_config_wlan[] = {
        /* CE0: host->target HTC control and raw streams */
        {
                .flags = CE_ATTR_FLAGS,
                .src_nentries = 16,
                .src_sz_max = 256,
                .dest_nentries = 0,
                .send_cb = ath10k_pci_htc_tx_cb,
        },

        /* CE1: target->host HTT + HTC control */
        {
                .flags = CE_ATTR_FLAGS,
                .src_nentries = 0,
                .src_sz_max = 2048,
                .dest_nentries = 512,
                .recv_cb = ath10k_pci_htt_htc_rx_cb,
        },

        /* CE2: target->host WMI */
        {
                .flags = CE_ATTR_FLAGS,
                .src_nentries = 0,
                .src_sz_max = 2048,
                .dest_nentries = 128,
                .recv_cb = ath10k_pci_htc_rx_cb,
        },

        /* CE3: host->target WMI */
        {
                .flags = CE_ATTR_FLAGS,
                .src_nentries = 32,
                .src_sz_max = 2048,
                .dest_nentries = 0,
                .send_cb = ath10k_pci_htc_tx_cb,
        },

        /* CE4: host->target HTT */
        {
                .flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
                .src_nentries = CE_HTT_H2T_MSG_SRC_NENTRIES,
                .src_sz_max = 256,
                .dest_nentries = 0,
                .send_cb = ath10k_pci_htt_tx_cb,
        },

        /* CE5: target->host HTT (HIF->HTT) */
        {
                .flags = CE_ATTR_FLAGS,
                .src_nentries = 0,
                .src_sz_max = 512,
                .dest_nentries = 512,
                .recv_cb = ath10k_pci_htt_rx_cb,
        },

        /* CE6: target autonomous hif_memcpy */
        {
                .flags = CE_ATTR_FLAGS,
                .src_nentries = 0,
                .src_sz_max = 0,
                .dest_nentries = 0,
        },

        /* CE7: ce_diag, the Diagnostic Window */
        {
                .flags = CE_ATTR_FLAGS,
                .src_nentries = 2,
                .src_sz_max = DIAG_TRANSFER_LIMIT,
                .dest_nentries = 2,
        },

        /* CE8: target->host pktlog */
        {
                .flags = CE_ATTR_FLAGS,
                .src_nentries = 0,
                .src_sz_max = 2048,
                .dest_nentries = 128,
                .recv_cb = ath10k_pci_pktlog_rx_cb,
        },

        /* CE9 target autonomous qcache memcpy */
        {
                .flags = CE_ATTR_FLAGS,
                .src_nentries = 0,
                .src_sz_max = 0,
                .dest_nentries = 0,
        },

        /* CE10: target autonomous hif memcpy */
        {
                .flags = CE_ATTR_FLAGS,
                .src_nentries = 0,
                .src_sz_max = 0,
                .dest_nentries = 0,
        },

        /* CE11: target autonomous hif memcpy */
        {
                .flags = CE_ATTR_FLAGS,
                .src_nentries = 0,
                .src_sz_max = 0,
                .dest_nentries = 0,
        },
};

/* 221 */
/* Target firmware's Copy Engine configuration. */
static struct ce_pipe_config target_ce_config_wlan[] = {
        /* CE0: host->target HTC control and raw streams */
        {
                .pipenum = 0,
                .pipedir = PIPEDIR_OUT,
                .nentries = 32,
                .nbytes_max = 256,
                .flags = CE_ATTR_FLAGS,
                .reserved = 0,
        },

        /* CE1: target->host HTT + HTC control */
        {
                .pipenum = 1,
                .pipedir = PIPEDIR_IN,
                .nentries = 32,
                .nbytes_max = 2048,
                .flags = CE_ATTR_FLAGS,
                .reserved = 0,
        },

        /* CE2: target->host WMI */
        {
                .pipenum = 2,
                .pipedir = PIPEDIR_IN,
                .nentries = 64,
                .nbytes_max = 2048,
                .flags = CE_ATTR_FLAGS,
                .reserved = 0,
        },

        /* CE3: host->target WMI */
        {
                .pipenum = 3,
                .pipedir = PIPEDIR_OUT,
                .nentries = 32,
                .nbytes_max = 2048,
                .flags = CE_ATTR_FLAGS,
                .reserved = 0,
        },

        /* CE4: host->target HTT */
        {
                .pipenum = 4,
                .pipedir = PIPEDIR_OUT,
                .nentries = 256,
                .nbytes_max = 256,
                .flags = CE_ATTR_FLAGS,
                .reserved = 0,
        },

        /* NB: 50% of src nentries, since tx has 2 frags */

        /* CE5: target->host HTT (HIF->HTT) */
        {
                .pipenum = 5,
                .pipedir = PIPEDIR_IN,
                .nentries = 32,
                .nbytes_max = 512,
                .flags = CE_ATTR_FLAGS,
                .reserved = 0,
        },

        /* CE6: Reserved for target autonomous hif_memcpy */
        {
                .pipenum = 6,
                .pipedir = PIPEDIR_INOUT,
                .nentries = 32,
                .nbytes_max = 4096,
                .flags = CE_ATTR_FLAGS,
                .reserved = 0,
        },

        /* CE7 used only by Host */
        {
                .pipenum = 7,
                .pipedir = PIPEDIR_INOUT,
                .nentries = 0,
                .nbytes_max = 0,
                .flags = 0,
                .reserved = 0,
        },

        /* CE8 target->host packtlog */
        {
                .pipenum = 8,
                .pipedir = PIPEDIR_IN,
                .nentries = 64,
                .nbytes_max = 2048,
                .flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
                .reserved = 0,
        },

        /* CE9 target autonomous qcache memcpy */
        {
                .pipenum = 9,
                .pipedir = PIPEDIR_INOUT,
                .nentries = 32,
                .nbytes_max = 2048,
                .flags = CE_ATTR_FLAGS | CE_ATTR_DIS_INTR,
                .reserved = 0,
        },

        /* It not necessary to send target wlan configuration for CE10 & CE11
         * as these CEs are not actively used in target.
         */
};

/* 330 */
/*
 * Map from service/endpoint to Copy Engine.
 * This table is derived from the CE_PCI TABLE, above.
 * It is passed to the Target at startup for use by firmware.
 */
static struct service_to_pipe target_service_to_ce_map_wlan[] = {
        {
                ATH10K_HTC_SVC_ID_WMI_DATA_VO,
                PIPEDIR_OUT,     /* out = UL = host -> target */
                3,
        },
        {
                ATH10K_HTC_SVC_ID_WMI_DATA_VO,
                PIPEDIR_IN,      /* in = DL = target -> host */
                2,
        },
        {
                ATH10K_HTC_SVC_ID_WMI_DATA_BK,
                PIPEDIR_OUT,     /* out = UL = host -> target */
                3,
        },
        {
                ATH10K_HTC_SVC_ID_WMI_DATA_BK,
                PIPEDIR_IN,      /* in = DL = target -> host */
                2,
        },
        {
                ATH10K_HTC_SVC_ID_WMI_DATA_BE,
                PIPEDIR_OUT,     /* out = UL = host -> target */
                3,
        },
        {
                ATH10K_HTC_SVC_ID_WMI_DATA_BE,
                PIPEDIR_IN,      /* in = DL = target -> host */
                2,
        },
        {
                ATH10K_HTC_SVC_ID_WMI_DATA_VI,
                PIPEDIR_OUT,     /* out = UL = host -> target */
                3,
        },
        {
                ATH10K_HTC_SVC_ID_WMI_DATA_VI,
                PIPEDIR_IN,      /* in = DL = target -> host */
                2,
        },
        {
                ATH10K_HTC_SVC_ID_WMI_CONTROL,
                PIPEDIR_OUT,     /* out = UL = host -> target */
                3,
        },
        {
                ATH10K_HTC_SVC_ID_WMI_CONTROL,
                PIPEDIR_IN,      /* in = DL = target -> host */
                2,
        },
        {
                ATH10K_HTC_SVC_ID_RSVD_CTRL,
                PIPEDIR_OUT,     /* out = UL = host -> target */
                0,
        },
        {
                ATH10K_HTC_SVC_ID_RSVD_CTRL,
                PIPEDIR_IN,      /* in = DL = target -> host */
                1,
        },
        { /* not used */
                ATH10K_HTC_SVC_ID_TEST_RAW_STREAMS,
                PIPEDIR_OUT,     /* out = UL = host -> target */
                0,
        },
        { /* not used */
                ATH10K_HTC_SVC_ID_TEST_RAW_STREAMS,
                PIPEDIR_IN,      /* in = DL = target -> host */
                1,
        },
        {
                ATH10K_HTC_SVC_ID_HTT_DATA_MSG,
                PIPEDIR_OUT,     /* out = UL = host -> target */
                4,
        },
        {
                ATH10K_HTC_SVC_ID_HTT_DATA_MSG,
                PIPEDIR_IN,      /* in = DL = target -> host */
                5,
        },

        /* (Additions here) */

        { /* must be last */
                0,
                0,
                0,
        },
};

/* 528 */
static int ath10k_pci_wake(struct ath10k *ar)
{
	/* TODO: Power-save mode */
	return 0;
}

/* 563 */
static void ath10k_pci_sleep(struct ath10k *ar)
{
	/* TODO: Power-save mode */
	return;
}

/* 626 */
static void ath10k_bus_pci_write32(struct ath10k *ar, uint32_t offset, uint32_t value)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
        int ret;

        if (unlikely(offset + sizeof(value) > ar_pci->mem_len)) {
                ath10k_warn("refusing to write mmio out of bounds at 0x%08x - 0x%08zx (max 0x%08zx)\n",
                            offset, offset + sizeof(value), ar_pci->mem_len);
                return;
        }

        ret = ath10k_pci_wake(ar);
        if (ret) {
                ath10k_warn("failed to wake target for write32 of 0x%08x at 0x%08x: %d\n",
                            value, offset, ret);
                return;
        }

        iowrite32(value, ar_pci->mem + offset);
        ath10k_pci_sleep(ar);
}

/* 648 */
static uint32_t ath10k_bus_pci_read32(struct ath10k *ar, uint32_t offset)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
        uint32_t val;
        int ret;

        if (unlikely(offset + sizeof(val) > ar_pci->mem_len)) {
                ath10k_warn("refusing to read mmio out of bounds at 0x%08x - 0x%08zx (max 0x%08zx)\n",
                            offset, offset + sizeof(val), ar_pci->mem_len);
                return 0;
        }

        ret = ath10k_pci_wake(ar);
        if (ret) {
                ath10k_warn("failed to wake target for read32 at 0x%08x: %d\n",
                            offset, ret);
                return 0xffffffff;
        }

        val = ioread32(ar_pci->mem + offset);
        ath10k_pci_sleep(ar);

        return val;
}

/* 673 */
inline void ath10k_pci_write32(struct ath10k *ar, uint32_t offset, uint32_t value)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

        ar_pci->bus_ops->write32(ar, offset, value);
}

/* 680 */
inline uint32_t ath10k_pci_read32(struct ath10k *ar, uint32_t offset)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

        return ar_pci->bus_ops->read32(ar, offset);
}

/* 687 */
uint32_t ath10k_pci_soc_read32(struct ath10k *ar, uint32_t addr)
{
        return ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS + addr);
}

/* 692 */
void ath10k_pci_soc_write32(struct ath10k *ar, uint32_t addr, uint32_t val)
{
        ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS + addr, val);
}

/* 697 */
uint32_t ath10k_pci_reg_read32(struct ath10k *ar, uint32_t addr)
{
        return ath10k_pci_read32(ar, PCIE_LOCAL_BASE_ADDRESS + addr);
}

void ath10k_pci_reg_write32(struct ath10k *ar, uint32_t addr, uint32_t val)
{
        ath10k_pci_write32(ar, PCIE_LOCAL_BASE_ADDRESS + addr, val);
}

/* 720 */
void ath10k_pci_disable_and_clear_legacy_irq(struct ath10k *ar)
{
        /* IMPORTANT: INTR_CLR register has to be set after
         * INTR_ENABLE is set to 0, otherwise interrupt can not be
         * really cleared.
         */
        ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_ENABLE_ADDRESS,
                           0);
        ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_CLR_ADDRESS,
                           PCIE_INTR_FIRMWARE_MASK | PCIE_INTR_CE_MASK_ALL);

        /* IMPORTANT: this extra read transaction is required to
         * flush the posted write buffer.
         */
        (void)ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS +
                                PCIE_INTR_ENABLE_ADDRESS);
}

/* 738 */
void ath10k_pci_enable_legacy_irq(struct ath10k *ar)
{
        ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS +
                           PCIE_INTR_ENABLE_ADDRESS,
                           PCIE_INTR_FIRMWARE_MASK | PCIE_INTR_CE_MASK_ALL);

        /* IMPORTANT: this extra read transaction is required to
         * flush the posted write buffer.
         */
        (void)ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS +
                                PCIE_INTR_ENABLE_ADDRESS);
}

/* 751 */
static inline const char *ath10k_pci_get_irq_method(struct ath10k *ar)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

        if (ar_pci->oper_irq_mode == ATH10K_PCI_IRQ_MSI)
                return "msi";

        return "legacy";
}

/* 847 */
static uint32_t ath10k_pci_qca988x_targ_cpu_to_ce_addr(struct ath10k *ar, uint32_t addr)
{
        uint32_t val = 0, region = addr & 0xfffff;

        val = (ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS + CORE_CTRL_ADDRESS)
                                 & 0x7ff) << 21;
        val |= 0x100000 | region;
        return val;
}

/* 857 */
static uint32_t ath10k_pci_qca99x0_targ_cpu_to_ce_addr(struct ath10k *ar, uint32_t addr)
{
        uint32_t val = 0, region = addr & 0xfffff;

        val = ath10k_pci_read32(ar, PCIE_BAR_REG_ADDRESS);
        val |= 0x100000 | region;
        return val;
}

/* 866 */
static zx_status_t ath10k_pci_targ_cpu_to_ce_addr(struct ath10k *ar, uint32_t cpu_addr,
						  uint32_t *ce_addr)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

        if (WARN_ON_ONCE(!ar_pci->targ_cpu_to_ce_addr)) {
                return ZX_ERR_NOT_SUPPORTED;
	}

        *ce_addr = ar_pci->targ_cpu_to_ce_addr(ar, cpu_addr);
	return ZX_OK;
}

/* 876 */
/*
 * Diagnostic read/write access is provided for startup/config/debug usage.
 * Caller must guarantee proper alignment, when applicable, and single user
 * at any moment.
 */
static zx_status_t ath10k_pci_diag_read_mem(struct ath10k *ar, uint32_t address, void *data,
	                                    int nbytes)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
        zx_status_t ret = ZX_OK;
        uint32_t *buf;
        unsigned int completed_nbytes, alloc_nbytes, remaining_bytes;
        struct ath10k_ce_pipe *ce_diag;
        /* Host buffer address in CE space */
        uint32_t ce_data;
        zx_paddr_t ce_data_base = 0;
	io_buffer_t ce_buf_handle;
        void *data_buf = NULL;
        int i;

        pthread_spin_lock(&ar_pci->ce_lock);

        ce_diag = ar_pci->ce_diag;

        /*
         * Allocate a temporary buffer to hold caller's data
         * to be DMA'ed from Target.
         */
        alloc_nbytes = min_t(unsigned int, nbytes, DIAG_TRANSFER_LIMIT);

	ret = io_buffer_init(&ce_buf_handle, alloc_nbytes, IO_BUFFER_RO | IO_BUFFER_CONTIG);
	if (ret != ZX_OK) {
		goto done;
	}
	data_buf = io_buffer_virt(&ce_buf_handle);
	ce_data_base = io_buffer_phys(&ce_buf_handle);
	ZX_DEBUG_ASSERT((ce_data_base + alloc_nbytes) <= 0x100000000ULL);
        remaining_bytes = nbytes;
        ce_data = ce_data_base;
        while (remaining_bytes) {
                nbytes = min_t(unsigned int, remaining_bytes,
                               DIAG_TRANSFER_LIMIT);

                ret = __ath10k_ce_rx_post_buf(ce_diag, &ce_data, ce_data);
                if (ret != ZX_OK) {
                        goto done;
		}

                /* Request CE to send from Target(!) address to Host buffer */
                /*
                 * The address supplied by the caller is in the
                 * Target CPU virtual address space.
                 *
                 * In order to use this address with the diagnostic CE,
                 * convert it from Target CPU virtual address space
                 * to CE address space
                 */
                ret = ath10k_pci_targ_cpu_to_ce_addr(ar, address, &address);
		if (ret != ZX_OK) {
			goto done;
		}

                ret = ath10k_ce_send_nolock(ce_diag, NULL, (uint32_t)address, nbytes, 0,
                                            0);
                if (ret != ZX_OK)
                        goto done;

                i = 0;
                while (ath10k_ce_completed_send_next_nolock(ce_diag,
                                                            NULL) != ZX_OK) {
                        mdelay(1);
                        if (i++ > DIAG_ACCESS_CE_TIMEOUT_MS) {
                                ret = ZX_ERR_TIMED_OUT;
                                goto done;
                        }
                }

                i = 0;
                while (ath10k_ce_completed_recv_next_nolock(ce_diag,
                                                            (void **)&buf,
                                                            &completed_nbytes)
                                                                != ZX_OK) {
                        mdelay(1);

                        if (i++ > DIAG_ACCESS_CE_TIMEOUT_MS) {
                                ret = ZX_ERR_TIMED_OUT;
                                goto done;
                        }
                }

                if ((unsigned int)nbytes != completed_nbytes) {
                        ret = ZX_ERR_IO;
                        goto done;
                }

                if (*buf != ce_data) {
                        ret = ZX_ERR_IO;
                        goto done;
                }

                remaining_bytes -= nbytes;
                memcpy(data, data_buf, nbytes);

                address += nbytes;
                data += nbytes;
        }

done:
#if 0
        if (data_buf) {
		io_buffer_release(&ce_buf_handle);
	}
#endif

        pthread_spin_unlock(&ar_pci->ce_lock);

        return ret;
}

/* 994 */
static zx_status_t ath10k_pci_diag_read32(struct ath10k *ar, uint32_t address, uint32_t *value)
{
        return ath10k_pci_diag_read_mem(ar, address, value, sizeof(*value));
}

/* 1033 */
zx_status_t ath10k_pci_diag_write_mem(struct ath10k *ar, uint32_t address,
				      const void *data, int nbytes)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
        zx_status_t ret = ZX_OK;
        uint32_t *buf;
        unsigned int completed_nbytes, orig_nbytes, remaining_bytes;
        struct ath10k_ce_pipe *ce_diag;
        void *data_buf = NULL;
        uint32_t ce_data;    /* Host buffer address in CE space */
        zx_paddr_t ce_data_base = 0;
	io_buffer_t ce_buf_handle;
        int i;

        pthread_spin_lock(&ar_pci->ce_lock);

        ce_diag = ar_pci->ce_diag;

        /*
         * Allocate a temporary buffer to hold caller's data
         * to be DMA'ed to Target.
         */
        orig_nbytes = nbytes;
	ret = io_buffer_init(&ce_buf_handle, orig_nbytes, IO_BUFFER_RW | IO_BUFFER_CONTIG);
	if (ret != ZX_OK) {
		goto done;
	}
	data_buf = io_buffer_virt(&ce_buf_handle);
	ce_data_base = io_buffer_phys(&ce_buf_handle);
	ZX_DEBUG_ASSERT((ce_data_base + orig_nbytes) <= 0x100000000ULL);

        /* Copy caller's data to allocated DMA buf */
        memcpy(data_buf, data, orig_nbytes);

        /*
         * The address supplied by the caller is in the
         * Target CPU virtual address space.
         *
         * In order to use this address with the diagnostic CE,
         * convert it from
         *    Target CPU virtual address space
         * to
         *    CE address space
         */
        ret = ath10k_pci_targ_cpu_to_ce_addr(ar, address, &address);
	if (ret != ZX_OK) {
		goto done;
	}

        remaining_bytes = orig_nbytes;
        ce_data = ce_data_base;
        while (remaining_bytes) {
                /* FIXME: check cast */
                nbytes = min_t(int, remaining_bytes, DIAG_TRANSFER_LIMIT);

                /* Set up to receive directly into Target(!) address */
                ret = __ath10k_ce_rx_post_buf(ce_diag, &address, address);
                if (ret != ZX_OK) {
                        goto done;
		}

                /*
                 * Request CE to send caller-supplied data that
                 * was copied to bounce buffer to Target(!) address.
                 */
                ret = ath10k_ce_send_nolock(ce_diag, NULL, (uint32_t)ce_data,
                                            nbytes, 0, 0);
                if (ret != ZX_OK)
                        goto done;

                i = 0;
                while (ath10k_ce_completed_send_next_nolock(ce_diag,
                                                            NULL) != ZX_OK) {
                        mdelay(1);

                        if (i++ > DIAG_ACCESS_CE_TIMEOUT_MS) {
                                ret = ZX_ERR_TIMED_OUT;
                                goto done;
                        }
                }

                i = 0;
                while (ath10k_ce_completed_recv_next_nolock(ce_diag,
                                                            (void **)&buf,
                                                            &completed_nbytes)
                                                                != ZX_OK) {
                        mdelay(1);

                        if (i++ > DIAG_ACCESS_CE_TIMEOUT_MS) {
                                ret = ZX_ERR_TIMED_OUT;
                                goto done;
                        }
                }

                if ((unsigned int)nbytes != completed_nbytes) {
                        ret = ZX_ERR_IO;
                        goto done;
                }

                if (*buf != address) {
                        ret = ZX_ERR_IO;
                        goto done;
                }

                remaining_bytes -= nbytes;
                address += nbytes;
                ce_data += nbytes;
        }

done:
#if 0
        if (data_buf) {
		io_buffer_release(&ce_buf_handle);
        }
#endif

        if (ret != ZX_OK) {
                ath10k_warn("failed to write diag value at 0x%x: %s\n",
                            address, zx_status_get_string(ret));
	}

        pthread_spin_unlock(&ar_pci->ce_lock);

        return ret;
}

/* 1155 */
static int ath10k_pci_diag_write32(struct ath10k *ar, uint32_t address, uint32_t value)
{
        uint32_t val = value;

        return ath10k_pci_diag_write_mem(ar, address, &val, sizeof(val));
}

/* 1162 */
/* Called by lower (CE) layer when a send to Target completes. */
static void ath10k_pci_htc_tx_cb(struct ath10k_ce_pipe *ce_state)
{
#if 0
        struct ath10k *ar = ce_state->ar;
        struct sk_buff_head list;
        struct sk_buff *skb;

        __skb_queue_head_init(&list);
        while (ath10k_ce_completed_send_next(ce_state, (void **)&skb) == ZX_OK) {
                /* no need to call tx completion for NULL pointers */
                if (skb == NULL)
                        continue;

                __skb_queue_tail(&list, skb);
        }

        while ((skb = __skb_dequeue(&list)))
                ath10k_htc_tx_completion_handler(ar, skb);
#endif
}

/* 1282 */
/* Called by lower (CE) layer when data is received from the Target. */
static void ath10k_pci_htc_rx_cb(struct ath10k_ce_pipe *ce_state)
{
#if 0
        ath10k_pci_process_rx_cb(ce_state, ath10k_htc_rx_completion_handler);
#endif
}

/* 1288 */
static void ath10k_pci_htt_htc_rx_cb(struct ath10k_ce_pipe *ce_state)
{
#if 0
        /* CE4 polling needs to be done whenever CE pipe which transports
         * HTT Rx (target->host) is processed.
         */
        ath10k_ce_per_engine_service(ce_state->ar, 4);

        ath10k_pci_process_rx_cb(ce_state, ath10k_htc_rx_completion_handler);
#endif
}

/* 1298 */
/* Called by lower (CE) layer when data is received from the Target.
 * Only 10.4 firmware uses separate CE to transfer pktlog data.
 */
static void ath10k_pci_pktlog_rx_cb(struct ath10k_ce_pipe *ce_state)
{
#if 0
        ath10k_pci_process_rx_cb(ce_state,
                                 ath10k_htt_rx_pktlog_completion_handler);
#endif
}

/* 1307 */
/* Called by lower (CE) layer when a send to HTT Target completes. */
static void ath10k_pci_htt_tx_cb(struct ath10k_ce_pipe *ce_state)
{
#if 0
        struct ath10k *ar = ce_state->ar;
        struct sk_buff *skb;

        while (ath10k_ce_completed_send_next(ce_state, (void **)&skb) == ZX_OK) {
                /* no need to call tx completion for NULL pointers */
                if (!skb)
                        continue;

                dma_unmap_single(ar->dev, ATH10K_SKB_CB(skb)->paddr,
                                 skb->len, DMA_TO_DEVICE);
                ath10k_htt_hif_tx_complete(ar, skb);
        }
#endif
}

/* 1330 */
/* Called by lower (CE) layer when HTT data is received from the Target. */
static void ath10k_pci_htt_rx_cb(struct ath10k_ce_pipe *ce_state)
{
#if 0
        /* CE4 polling needs to be done whenever CE pipe which transports
         * HTT Rx (target->host) is processed.
         */
        ath10k_ce_per_engine_service(ce_state->ar, 4);

        ath10k_pci_process_htt_rx_cb(ce_state, ath10k_pci_htt_rx_deliver);
#endif
}

/* 1459 */
static void ath10k_pci_fw_crashed_dump(struct ath10k *ar)
{
#if 0
        struct ath10k_fw_crash_data *crash_data;
        char uuid[50];

        spin_lock_bh(&ar->data_lock);

        ar->stats.fw_crash_counter++;

        crash_data = ath10k_debug_get_new_fw_crash_data(ar);

        if (crash_data)
                scnprintf(uuid, sizeof(uuid), "%pUl", &crash_data->uuid);
        else
                scnprintf(uuid, sizeof(uuid), "n/a");

        ath10k_err("firmware crashed! (uuid %s)\n", uuid);
        ath10k_print_driver_info(ar);
        ath10k_pci_dump_registers(ar, crash_data);
        ath10k_ce_dump_registers(ar, crash_data);

        spin_unlock_bh(&ar->data_lock);

        queue_work(ar->workqueue, &ar->restart_work);
#endif
}

/* 1573 */
void ath10k_pci_irq_msi_fw_mask(struct ath10k *ar)
{
        uint32_t val;

        switch (ar->hw_rev) {
        case ATH10K_HW_QCA988X:
        case ATH10K_HW_QCA9887:
        case ATH10K_HW_QCA6174:
        case ATH10K_HW_QCA9377:
                val = ath10k_pci_read32(ar, SOC_CORE_BASE_ADDRESS +
                                        CORE_CTRL_ADDRESS);
                val &= ~CORE_CTRL_PCIE_REG_31_MASK;
                ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS +
                                   CORE_CTRL_ADDRESS, val);
                break;
        case ATH10K_HW_QCA99X0:
        case ATH10K_HW_QCA9984:
        case ATH10K_HW_QCA9888:
        case ATH10K_HW_QCA4019:
                /* TODO: Find appropriate register configuration for QCA99X0
                 *  to mask irq/MSI.
                 */
                break;
        }
}

/* 1625 */
static void ath10k_pci_irq_disable(struct ath10k *ar)
{
        ath10k_ce_disable_interrupts(ar);
        ath10k_pci_disable_and_clear_legacy_irq(ar);
        ath10k_pci_irq_msi_fw_mask(ar);
}

/* 1746 */
void ath10k_pci_ce_deinit(struct ath10k *ar)
{
        int i;

        for (i = 0; i < CE_COUNT; i++)
                ath10k_ce_deinit_pipe(ar, i);
}

/* 1791 */
zx_status_t ath10k_pci_hif_exchange_bmi_msg(struct ath10k *ar,
					    void *req, uint32_t req_len,
					    void *resp, uint32_t *resp_len)
{
	struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	struct ath10k_pci_pipe *pci_tx = &ar_pci->pipe_info[BMI_CE_NUM_TO_TARG];
	struct ath10k_pci_pipe *pci_rx = &ar_pci->pipe_info[BMI_CE_NUM_TO_HOST];
	struct ath10k_ce_pipe *ce_tx = pci_tx->ce_hdl;
	struct ath10k_ce_pipe *ce_rx = pci_rx->ce_hdl;
	zx_paddr_t req_paddr = 0;
	zx_paddr_t resp_paddr = 0;
	struct bmi_xfer xfer = {};
	io_buffer_t treq, tresp;
	void *req_vaddr, *resp_vaddr = NULL;
	zx_status_t ret = ZX_OK;

	if (resp && !resp_len)
		return ZX_ERR_INVALID_ARGS;

	if (resp && resp_len && *resp_len == 0)
		return ZX_ERR_INVALID_ARGS;

	ret = io_buffer_init(&treq, req_len, IO_BUFFER_RW | IO_BUFFER_CONTIG);
	if (ret != ZX_OK) {
		return ret;
	}
	req_vaddr = io_buffer_virt(&treq);
	memcpy(req_vaddr, req, req_len);

	req_paddr = io_buffer_phys(&treq);
	ZX_DEBUG_ASSERT((req_paddr + req_len) <= 0x100000000ULL);

	if (resp && resp_len) {
		if (io_buffer_init(&tresp, *resp_len, IO_BUFFER_RO | IO_BUFFER_CONTIG) != ZX_OK) {
			ret = ZX_ERR_NO_MEMORY;
			goto err_req;
		}
		resp_vaddr = io_buffer_virt(&tresp);
		resp_paddr = io_buffer_phys(&tresp);

		xfer.wait_for_resp = true;
		xfer.resp_len = 0;

		ath10k_ce_rx_post_buf(ce_rx, &xfer, resp_paddr);
	}

	ret = ath10k_ce_send(ce_tx, &xfer, req_paddr, req_len, -1, 0);
	if (ret != ZX_OK) {
		goto err_resp;
	}

	ret = ath10k_pci_bmi_wait(ar, ce_tx, ce_rx, &xfer);
	if (ret != ZX_OK) {
		uint32_t unused_buffer;
		unsigned int unused_nbytes;
		unsigned int unused_id;

		ath10k_ce_cancel_send_next(ce_tx, NULL, &unused_buffer,
					   &unused_nbytes, &unused_id);
	} else {
		/* non-zero means we did not time out */
		ret = ZX_OK;
	}

err_resp:
	if (resp) {
		uint32_t unused_buffer;

		ath10k_ce_revoke_recv_next(ce_rx, NULL, &unused_buffer);
	}
err_req:
	if (ret == ZX_OK && resp_len) {
		*resp_len = min(*resp_len, xfer.resp_len);
		memcpy(resp, resp_vaddr, xfer.resp_len);
	}
	io_buffer_release(&treq);
	io_buffer_release(&tresp);

	return ret;
}

/* 1885 */
static void ath10k_pci_bmi_send_done(struct ath10k_ce_pipe *ce_state)
{
        struct bmi_xfer *xfer;

        if (ath10k_ce_completed_send_next(ce_state, (void **)&xfer))
                return;

        xfer->tx_done = true;
}

/* 1895 */
static void ath10k_pci_bmi_recv_data(struct ath10k_ce_pipe *ce_state)
{
        struct bmi_xfer *xfer;
        unsigned int nbytes;

        if (ath10k_ce_completed_recv_next(ce_state, (void **)&xfer,
                                          &nbytes))
                return;

        if (WARN_ON_ONCE(!xfer))
                return;

        if (!xfer->wait_for_resp) {
                ath10k_warn("unexpected: BMI data received; ignoring\n");
                return;
        }

        xfer->resp_len = nbytes;
        xfer->rx_done = true;
}

/* 1917 */
static zx_status_t ath10k_pci_bmi_wait(struct ath10k *ar,
				       struct ath10k_ce_pipe *tx_pipe,
				       struct ath10k_ce_pipe *rx_pipe,
				       struct bmi_xfer *xfer)
{
	zx_time_t started = zx_time_get(ZX_CLOCK_MONOTONIC);
	zx_time_t timeout = started + BMI_COMMUNICATION_TIMEOUT;
	zx_time_t now, dur;
	zx_status_t ret;

	do {
		ath10k_pci_bmi_send_done(tx_pipe);
		ath10k_pci_bmi_recv_data(rx_pipe);

                if (xfer->tx_done && (xfer->rx_done == xfer->wait_for_resp)) {
                        ret = ZX_OK;
                        goto out;
                }

                sched_yield();

		now = zx_time_get(ZX_CLOCK_MONOTONIC);
	} while (now < timeout);

        ret = ZX_ERR_TIMED_OUT;

out:
        dur = now - started;
        if (dur > ZX_SEC(1)) {
		double secs_elapsed = (double)(now - started) / ZX_SEC(1);
                ath10k_dbg(ar, ATH10K_DBG_BMI,
                           "bmi cmd took %0.2d secs, failed with %s\n",
                           secs_elapsed, zx_status_get_string(ret));
	}
        return ret;
}

/* 1950 */
/*
 * Send an interrupt to the device to wake up the Target CPU
 * so it has an opportunity to notice any changed state.
 */
static zx_status_t ath10k_pci_wake_target_cpu(struct ath10k *ar)
{
        uint32_t addr, val;

        addr = SOC_CORE_BASE_ADDRESS + CORE_CTRL_ADDRESS;
        val = ath10k_pci_read32(ar, addr);
        val |= CORE_CTRL_CPU_INTR_MASK;
        ath10k_pci_write32(ar, addr, val);

        return ZX_OK;
}

/* 1966 */
static int ath10k_pci_get_num_banks(struct ath10k *ar)
{
        switch (ar->id.device) {
        case QCA988X_2_0_DEVICE_ID:
        case QCA99X0_2_0_DEVICE_ID:
        case QCA9888_2_0_DEVICE_ID:
        case QCA9984_1_0_DEVICE_ID:
        case QCA9887_1_0_DEVICE_ID:
                return 1;
        case QCA6164_2_1_DEVICE_ID:
        case QCA6174_2_1_DEVICE_ID:
                switch (MS(ar->chip_id, SOC_CHIP_ID_REV)) {
                case QCA6174_HW_1_0_CHIP_ID_REV:
                case QCA6174_HW_1_1_CHIP_ID_REV:
                case QCA6174_HW_2_1_CHIP_ID_REV:
                case QCA6174_HW_2_2_CHIP_ID_REV:
                        return 3;
                case QCA6174_HW_1_3_CHIP_ID_REV:
                        return 2;
                case QCA6174_HW_3_0_CHIP_ID_REV:
                case QCA6174_HW_3_1_CHIP_ID_REV:
                case QCA6174_HW_3_2_CHIP_ID_REV:
                        return 9;
                }
                break;
        case QCA9377_1_0_DEVICE_ID:
                return 4;
        }

        ath10k_warn("unknown number of banks, assuming 1\n");
        return 1;
}

/* 2001 */
static int ath10k_bus_get_num_banks(struct ath10k *ar)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

        return ar_pci->bus_ops->get_num_banks(ar);
}

/* 2008 */
zx_status_t ath10k_pci_init_config(struct ath10k *ar)
{
	uint32_t interconnect_targ_addr;
	uint32_t pcie_state_targ_addr = 0;
	uint32_t pipe_cfg_targ_addr = 0;
	uint32_t svc_to_pipe_map = 0;
	uint32_t pcie_config_flags = 0;
	uint32_t ealloc_value;
	uint32_t ealloc_targ_addr;
	uint32_t flag2_value;
	uint32_t flag2_targ_addr;
	zx_status_t ret = ZX_OK;

	/* Download to Target the CE Config and the service-to-CE map */
	interconnect_targ_addr =
		host_interest_item_address(HI_ITEM(hi_interconnect_state));

	/* Supply Target-side CE configuration */
	ret = ath10k_pci_diag_read32(ar, interconnect_targ_addr,
				     &pcie_state_targ_addr);
	if (ret != ZX_OK) {
		ath10k_err("Failed to get pcie state addr: %s\n", zx_status_get_string(ret));
		return ret;
	}

	if (pcie_state_targ_addr == 0) {
		ret = ZX_ERR_IO;
		ath10k_err("Invalid pcie state addr\n");
		return ret;
	}

	ret = ath10k_pci_diag_read32(ar, (pcie_state_targ_addr +
					  offsetof(struct pcie_state,
						   pipe_cfg_addr)),
				     &pipe_cfg_targ_addr);
	if (ret != ZX_OK) {
		ath10k_err("Failed to get pipe cfg addr: %s\n", zx_status_get_string(ret));
		return ret;
	}

	if (pipe_cfg_targ_addr == 0) {
		ret = ZX_ERR_IO;
		ath10k_err("Invalid pipe cfg addr\n");
		return ret;
	}

	ret = ath10k_pci_diag_write_mem(ar, pipe_cfg_targ_addr,
					target_ce_config_wlan,
					sizeof(struct ce_pipe_config) *
					NUM_TARGET_CE_CONFIG_WLAN);

	if (ret != ZX_OK) {
		ath10k_err("Failed to write pipe cfg: %s\n", zx_status_get_string(ret));
		return ret;
	}

	ret = ath10k_pci_diag_read32(ar, (pcie_state_targ_addr +
					  offsetof(struct pcie_state,
						   svc_to_pipe_map)),
				     &svc_to_pipe_map);
	if (ret != ZX_OK) {
		ath10k_err("Failed to get svc/pipe map: %s\n", zx_status_get_string(ret));
		return ret;
	}

	if (svc_to_pipe_map == 0) {
		ret = ZX_ERR_IO;
		ath10k_err("Invalid svc_to_pipe map\n");
		return ret;
	}

	ret = ath10k_pci_diag_write_mem(ar, svc_to_pipe_map,
					target_service_to_ce_map_wlan,
					sizeof(target_service_to_ce_map_wlan));
	if (ret != ZX_OK) {
		ath10k_err("Failed to write svc/pipe map: %s\n", zx_status_get_string(ret));
		return ret;
	}

	ret = ath10k_pci_diag_read32(ar, (pcie_state_targ_addr +
					  offsetof(struct pcie_state,
						   config_flags)),
				     &pcie_config_flags);
	if (ret != ZX_OK) {
		ath10k_err("Failed to get pcie config_flags: %s\n", zx_status_get_string(ret));
		return ret;
	}

	pcie_config_flags &= ~PCIE_CONFIG_FLAG_ENABLE_L1;

	ret = ath10k_pci_diag_write32(ar, (pcie_state_targ_addr +
					   offsetof(struct pcie_state,
						    config_flags)),
				      pcie_config_flags);
	if (ret != ZX_OK) {
		ath10k_err("Failed to write pcie config_flags: %s\n",
			   zx_status_get_string(ret));
		return ret;
	}

	/* configure early allocation */
	ealloc_targ_addr = host_interest_item_address(HI_ITEM(hi_early_alloc));

	ret = ath10k_pci_diag_read32(ar, ealloc_targ_addr, &ealloc_value);
	if (ret != ZX_OK) {
		ath10k_err("Failed to get early alloc val: %s\n", zx_status_get_string(ret));
		return ret;
	}

	/* first bank is switched to IRAM */
	ealloc_value |= ((HI_EARLY_ALLOC_MAGIC << HI_EARLY_ALLOC_MAGIC_SHIFT) &
			 HI_EARLY_ALLOC_MAGIC_MASK);
	ealloc_value |= ((ath10k_bus_get_num_banks(ar) <<
			  HI_EARLY_ALLOC_IRAM_BANKS_SHIFT) &
			 HI_EARLY_ALLOC_IRAM_BANKS_MASK);

	ret = ath10k_pci_diag_write32(ar, ealloc_targ_addr, ealloc_value);
	if (ret != ZX_OK) {
		ath10k_err("Failed to set early alloc val: %s\n", zx_status_get_string(ret));
		return ret;
	}

	/* Tell Target to proceed with initialization */
	flag2_targ_addr = host_interest_item_address(HI_ITEM(hi_option_flag2));

	ret = ath10k_pci_diag_read32(ar, flag2_targ_addr, &flag2_value);
	if (ret != ZX_OK) {
		ath10k_err("Failed to get option val: %s\n", zx_status_get_string(ret));
		return ret;
	}

	flag2_value |= HI_OPTION_EARLY_CFG_DONE;

	ret = ath10k_pci_diag_write32(ar, flag2_targ_addr, flag2_value);
	if (ret != ZX_OK) {
		ath10k_err("Failed to set option val: %s\n", zx_status_get_string(ret));
		return ret;
	}

	return ZX_OK;
}

/* 2149 */
static void ath10k_pci_override_ce_config(struct ath10k *ar)
{
        struct ce_attr *attr;
        struct ce_pipe_config *config;

        /* For QCA6174 we're overriding the Copy Engine 5 configuration,
         * since it is currently used for other feature.
         */

        /* Override Host's Copy Engine 5 configuration */
        attr = &host_ce_config_wlan[5];
        attr->src_sz_max = 0;
        attr->dest_nentries = 0;

        /* Override Target firmware's Copy Engine configuration */
        config = &target_ce_config_wlan[5];
        config->pipedir = PIPEDIR_OUT;
        config->nbytes_max = 2048;

        /* Map from service/endpoint to Copy Engine */
        target_service_to_ce_map_wlan[15].pipenum = 1;
}

/* 2172 */
zx_status_t ath10k_pci_alloc_pipes(struct ath10k *ar)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
        struct ath10k_pci_pipe *pipe;
        int i;
	zx_status_t ret;

        for (i = 0; i < CE_COUNT; i++) {
                pipe = &ar_pci->pipe_info[i];
                pipe->ce_hdl = &ar_pci->ce_states[i];
                pipe->pipe_num = i;
                pipe->hif_ce_state = ar;

                ret = ath10k_ce_alloc_pipe(ar, i, &host_ce_config_wlan[i]);
                if (ret != ZX_OK) {
                        ath10k_err("failed to allocate copy engine pipe %d: %s\n",
                                   i, zx_status_get_string(ret));
                        return ret;
                }

                /* Last CE is Diagnostic Window */
                if (i == CE_DIAG_PIPE) {
                        ar_pci->ce_diag = pipe->ce_hdl;
                        continue;
                }

                pipe->buf_sz = (size_t)(host_ce_config_wlan[i].src_sz_max);
        }
        return ZX_OK;
}

/* 2203 */
void ath10k_pci_free_pipes(struct ath10k *ar)
{
        int i;

        for (i = 0; i < CE_COUNT; i++)
                ath10k_ce_free_pipe(ar, i);
}

/* 2211 */
zx_status_t ath10k_pci_init_pipes(struct ath10k *ar)
{
        int i;
        zx_status_t ret;

        for (i = 0; i < CE_COUNT; i++) {
                ret = ath10k_ce_init_pipe(ar, i, &host_ce_config_wlan[i]);
                if (ret != ZX_OK) {
                        ath10k_err("failed to initialize copy engine pipe %d: %s\n",
                                   i, zx_status_get_string(ret));
                        return ret;
                }
        }

        return 0;
}

/* 2227 */
static bool ath10k_pci_has_fw_crashed(struct ath10k *ar)
{
        return ath10k_pci_read32(ar, FW_INDICATOR_ADDRESS) &
               FW_IND_EVENT_PENDING;
}

/* 2233 */
static void ath10k_pci_fw_crashed_clear(struct ath10k *ar)
{
        uint32_t val;

        val = ath10k_pci_read32(ar, FW_INDICATOR_ADDRESS);
        val &= ~FW_IND_EVENT_PENDING;
        ath10k_pci_write32(ar, FW_INDICATOR_ADDRESS, val);
}

/* 2250 */
/* this function effectively clears target memory controller assert line */
static void ath10k_pci_warm_reset_si0(struct ath10k *ar)
{
        uint32_t val;

        val = ath10k_pci_soc_read32(ar, SOC_RESET_CONTROL_ADDRESS);
        ath10k_pci_soc_write32(ar, SOC_RESET_CONTROL_ADDRESS,
                               val | SOC_RESET_CONTROL_SI0_RST_MASK);
        val = ath10k_pci_soc_read32(ar, SOC_RESET_CONTROL_ADDRESS);

	zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));

        val = ath10k_pci_soc_read32(ar, SOC_RESET_CONTROL_ADDRESS);
        ath10k_pci_soc_write32(ar, SOC_RESET_CONTROL_ADDRESS,
                               val & ~SOC_RESET_CONTROL_SI0_RST_MASK);
        val = ath10k_pci_soc_read32(ar, SOC_RESET_CONTROL_ADDRESS);

	zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
}

/* 2270 */
static void ath10k_pci_warm_reset_cpu(struct ath10k *ar)
{
        uint32_t val;

        ath10k_pci_write32(ar, FW_INDICATOR_ADDRESS, 0);

        val = ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS +
                                SOC_RESET_CONTROL_ADDRESS);
        ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS + SOC_RESET_CONTROL_ADDRESS,
                           val | SOC_RESET_CONTROL_CPU_WARM_RST_MASK);
}

/* 2282 */
static void ath10k_pci_warm_reset_ce(struct ath10k *ar)
{
        uint32_t val;

        val = ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS +
                                SOC_RESET_CONTROL_ADDRESS);

        ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS + SOC_RESET_CONTROL_ADDRESS,
                           val | SOC_RESET_CONTROL_CE_RST_MASK);
	zx_nanosleep(zx_deadline_after(ZX_MSEC(10)));
        ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS + SOC_RESET_CONTROL_ADDRESS,
                           val & ~SOC_RESET_CONTROL_CE_RST_MASK);
}

/* 2296 */
static void ath10k_pci_warm_reset_clear_lf(struct ath10k *ar)
{
        uint32_t val;

        val = ath10k_pci_read32(ar, RTC_SOC_BASE_ADDRESS +
                                SOC_LF_TIMER_CONTROL0_ADDRESS);
        ath10k_pci_write32(ar, RTC_SOC_BASE_ADDRESS +
                           SOC_LF_TIMER_CONTROL0_ADDRESS,
                           val & ~SOC_LF_TIMER_CONTROL0_ENABLE_MASK);
}

/* 2307 */
static zx_status_t ath10k_pci_warm_reset(struct ath10k *ar)
{
        zx_status_t ret;

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot warm reset\n");

        pthread_spin_lock(&ar->data_lock);
        ar->stats.fw_warm_reset_counter++;
        pthread_spin_unlock(&ar->data_lock);

        ath10k_pci_irq_disable(ar);

        /* Make sure the target CPU is not doing anything dangerous, e.g. if it
         * were to access copy engine while host performs copy engine reset
         * then it is possible for the device to confuse pci-e controller to
         * the point of bringing host system to a complete stop (i.e. hang).
         */
        ath10k_pci_warm_reset_si0(ar);
        ath10k_pci_warm_reset_cpu(ar);
        ath10k_pci_init_pipes(ar);
        ath10k_pci_wait_for_target_init(ar);

        ath10k_pci_warm_reset_clear_lf(ar);
        ath10k_pci_warm_reset_ce(ar);
        ath10k_pci_warm_reset_cpu(ar);
        ath10k_pci_init_pipes(ar);

        ret = ath10k_pci_wait_for_target_init(ar);
        if (ret) {
                ath10k_warn("failed to wait for target init: %d\n", ret);
                return ret;
        }

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot warm reset complete\n");
        return ZX_OK;
}

/* 2345 */
static int ath10k_pci_qca99x0_soft_chip_reset(struct ath10k *ar)
{
        ath10k_pci_irq_disable(ar);
        return ath10k_pci_qca99x0_chip_reset(ar);
}

/* 2361 */
static zx_status_t ath10k_pci_qca988x_chip_reset(struct ath10k *ar)
{
        int i;
        zx_status_t ret;
        uint32_t val;

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot 988x chip reset\n");

        /* Some hardware revisions (e.g. CUS223v2) has issues with cold reset.
         * It is thus preferred to use warm reset which is safer but may not be
         * able to recover the device from all possible fail scenarios.
         *
         * Warm reset doesn't always work on first try so attempt it a few
         * times before giving up.
         */
        for (i = 0; i < ATH10K_PCI_NUM_WARM_RESET_ATTEMPTS; i++) {
                ret = ath10k_pci_warm_reset(ar);
                if (ret != ZX_OK) {
                        ath10k_warn("failed to warm reset attempt %d of %d: %s\n",
                                    i + 1, ATH10K_PCI_NUM_WARM_RESET_ATTEMPTS,
                                    zx_status_get_string(ret));
                        continue;
                }

                /* FIXME: Sometimes copy engine doesn't recover after warm
                 * reset. In most cases this needs cold reset. In some of these
                 * cases the device is in such a state that a cold reset may
                 * lock up the host.
                 *
                 * Reading any host interest register via copy engine is
                 * sufficient to verify if device is capable of booting
                 * firmware blob.
                 */
                ret = ath10k_pci_init_pipes(ar);
                if (ret != ZX_OK) {
                        ath10k_warn("failed to init copy engine: %s\n",
                                    zx_status_get_string(ret));
                        continue;
                }

                ret = ath10k_pci_diag_read32(ar, QCA988X_HOST_INTEREST_ADDRESS,
                                             &val);
                if (ret != ZX_OK) {
                        ath10k_warn("failed to poke copy engine: %s\n",
                                    zx_status_get_string(ret));
                        continue;
                }

                ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot chip reset complete (warm)\n");
                return 0;
        }

        if (ath10k_pci_reset_mode == ATH10K_PCI_RESET_WARM_ONLY) {
                ath10k_warn("refusing cold reset as requested\n");
                return ZX_ERR_NOT_SUPPORTED;
        }

        ret = ath10k_pci_cold_reset(ar);
        if (ret != ZX_OK) {
                ath10k_warn("failed to cold reset: %s\n", zx_status_get_string(ret));
                return ret;
        }

        ret = ath10k_pci_wait_for_target_init(ar);
        if (ret != ZX_OK) {
                ath10k_warn("failed to wait for target after cold reset: %s\n",
                            zx_status_get_string(ret));
                return ret;
        }

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot qca988x chip reset complete (cold)\n");

	return ZX_OK;
}

/* 2435 */
static zx_status_t ath10k_pci_qca6174_chip_reset(struct ath10k *ar)
{
        zx_status_t ret;

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot qca6174 chip reset\n");

        /* FIXME: QCA6174 requires cold + warm reset to work. */

        ret = ath10k_pci_cold_reset(ar);
        if (ret != ZX_OK) {
                ath10k_warn("failed to cold reset: %s\n", zx_status_get_string(ret));
                return ret;
        }

        ret = ath10k_pci_wait_for_target_init(ar);
        if (ret != ZX_OK) {
                ath10k_warn("failed to wait for target after cold reset: %s\n",
                            zx_status_get_string(ret));
                return ret;
        }

        ret = ath10k_pci_warm_reset(ar);
        if (ret != ZX_OK) {
                ath10k_warn("failed to warm reset: %s\n", zx_status_get_string(ret));
                return ret;
        }

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot qca6174 chip reset complete (cold)\n");

        return ZX_OK;
}

/* 2467 */
static zx_status_t ath10k_pci_qca99x0_chip_reset(struct ath10k *ar)
{
        zx_status_t ret;

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot qca99x0 chip reset\n");

        ret = ath10k_pci_cold_reset(ar);
        if (ret != ZX_OK) {
                ath10k_warn("failed to cold reset: %s\n", zx_status_get_string(ret));
                return ret;
        }

        ret = ath10k_pci_wait_for_target_init(ar);
        if (ret != ZX_OK) {
                ath10k_warn("failed to wait for target after cold reset: %s\n",
                            zx_status_get_string(ret));
                return ret;
        }

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot qca99x0 chip reset complete (cold)\n");

        return ZX_OK;
}

/* 2491 */
static zx_status_t ath10k_pci_chip_reset(struct ath10k *ar)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

        if (WARN_ON(!ar_pci->pci_hard_reset))
                return ZX_ERR_NOT_SUPPORTED;

        return ar_pci->pci_hard_reset(ar);
}

/* 2501 */
static zx_status_t ath10k_pci_hif_power_up(struct ath10k *ar)
{
        zx_status_t ret;

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot hif power up\n");

#if 0
/* Fuchsia note - get PCI express capability offset with kPciCapIdPciExpress and then
   read capability at offset + PCI_EXP_LNKCTL */
        pcie_capability_read_word(ar_pci->pdev, PCI_EXP_LNKCTL,
                                  &ar_pci->link_ctl);
        pcie_capability_write_word(ar_pci->pdev, PCI_EXP_LNKCTL,
                                   ar_pci->link_ctl & ~PCI_EXP_LNKCTL_ASPMC);
#endif

        /*
         * Bring the target up cleanly.
         *
         * The target may be in an undefined state with an AUX-powered Target
         * and a Host in WoW mode. If the Host crashes, loses power, or is
         * restarted (without unloading the driver) then the Target is left
         * (aux) powered and running. On a subsequent driver load, the Target
         * is in an unexpected state. We try to catch that here in order to
         * reset the Target and retry the probe.
         */
        ret = ath10k_pci_chip_reset(ar);
        if (ret != ZX_OK) {
                if (ath10k_pci_has_fw_crashed(ar)) {
                        ath10k_warn("firmware crashed during chip reset\n");
                        ath10k_pci_fw_crashed_clear(ar);
                        ath10k_pci_fw_crashed_dump(ar);
                }

                ath10k_err("failed to reset chip: %s\n", zx_status_get_string(ret));
                goto err_sleep;
        }

        ret = ath10k_pci_init_pipes(ar);
        if (ret != ZX_OK) {
                ath10k_err("failed to initialize CE: %s\n", zx_status_get_string(ret));
                goto err_sleep;
        }

        ret = ath10k_pci_init_config(ar);
        if (ret != ZX_OK) {
                ath10k_err("failed to setup init config: %s\n", zx_status_get_string(ret));
                goto err_ce;
        }

        ret = ath10k_pci_wake_target_cpu(ar);
        if (ret != ZX_OK) {
                ath10k_err("could not wake up target CPU: %s\n", zx_status_get_string(ret));
                goto err_ce;
        }

        return ZX_OK;

err_ce:
        ath10k_pci_ce_deinit(ar);

err_sleep:
        return ret;
}

/* 2562 */
void ath10k_pci_hif_power_down(struct ath10k *ar)
{
        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot hif power down\n");

        /* Currently hif_power_up performs effectively a reset and hif_stop
         * resets the chip as well so there's no point in resetting here.
         */
}

/* 2749 */
static const struct ath10k_hif_ops ath10k_pci_hif_ops = {
        .exchange_bmi_msg       = ath10k_pci_hif_exchange_bmi_msg,
        .power_up               = ath10k_pci_hif_power_up,
        .power_down             = ath10k_pci_hif_power_down,
};

/* 2874 */
static zx_status_t ath10k_pci_request_irq(struct ath10k *ar)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	pci_protocol_t *pdev = &ar_pci->pdev;
        zx_status_t ret;

	ret = pci_map_interrupt(pdev, 0, &ar_pci->irq_handle);
	if (ret != ZX_OK) {
		ath10k_err("couldn't map irq 0\n");
	}
	return ret;
}

/* 2888 */
static void ath10k_pci_free_irq(struct ath10k *ar)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

	zx_handle_close(ar_pci->irq_handle);
}

/* 2901 */
static zx_status_t ath10k_pci_init_irq(struct ath10k *ar)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	pci_protocol_t *pdev = &ar_pci->pdev;
	uint32_t irq_cnt = 0;

#if 0
        ath10k_pci_init_napi(ar);
#endif

        if (ath10k_pci_irq_mode != ATH10K_PCI_IRQ_AUTO)
                ath10k_info("limiting irq mode to: %d\n",
                            ath10k_pci_irq_mode);

        /* Try MSI */
        if ((ath10k_pci_irq_mode != ATH10K_PCI_IRQ_LEGACY)
	    && (pci_query_irq_mode_caps(pdev, ZX_PCIE_IRQ_MODE_MSI, &irq_cnt) == ZX_OK)
	    && (pci_set_irq_mode(pdev, ZX_PCIE_IRQ_MODE_MSI, 1) == ZX_OK)) {
               	ar_pci->oper_irq_mode = ATH10K_PCI_IRQ_MSI;
		return ZX_OK;
	}

        /* Try legacy irq
         *
         * A potential race occurs here: The CORE_BASE write
         * depends on target correctly decoding AXI address but
         * host won't know when target writes BAR to CORE_CTRL.
         * This write might get lost if target has NOT written BAR.
         * For now, fix the race by repeating the write in below
         * synchronization checking.
         */
	if ((pci_query_irq_mode_caps(pdev, ZX_PCIE_IRQ_MODE_LEGACY, &irq_cnt) == ZX_OK)
	    && (pci_set_irq_mode(pdev, ZX_PCIE_IRQ_MODE_LEGACY, 1) == ZX_OK)) {
        	ar_pci->oper_irq_mode = ATH10K_PCI_IRQ_LEGACY;

	        ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_ENABLE_ADDRESS,
				   PCIE_INTR_FIRMWARE_MASK | PCIE_INTR_CE_MASK_ALL);

	        return ZX_OK;
	}

	ath10k_err("failed to determine IRQ mode\n");
	return ZX_ERR_NOT_SUPPORTED;
}

/* 2939 */
static void ath10k_pci_deinit_irq_legacy(struct ath10k *ar)
{
        ath10k_pci_write32(ar, SOC_CORE_BASE_ADDRESS + PCIE_INTR_ENABLE_ADDRESS,
                           0);
}

/* 2945 */
static void ath10k_pci_deinit_irq(struct ath10k *ar)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);

        switch (ar_pci->oper_irq_mode) {
        case ATH10K_PCI_IRQ_LEGACY:
                ath10k_pci_deinit_irq_legacy(ar);
                break;
        default:
#if 0
		// TODO: How to disable MSI interrupts?
                pci_disable_msi(ar_pci->pdev);
#endif
                break;
        }
}

/* 2961 */
zx_status_t ath10k_pci_wait_for_target_init(struct ath10k *ar)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
	zx_time_t timeout;
        uint32_t val;

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot waiting target to initialise\n");

	timeout = zx_time_get(ZX_CLOCK_MONOTONIC) + ZX_MSEC(ATH10K_PCI_TARGET_WAIT);

        do {
                val = ath10k_pci_read32(ar, FW_INDICATOR_ADDRESS);

                ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot target indicator %x\n",
                           val);

                /* target should never return this */
                if (val == 0xffffffff)
                        continue;

                /* the device has crashed so don't bother trying anymore */
                if (val & FW_IND_EVENT_PENDING)
                        break;

                if (val & FW_IND_INITIALIZED)
                        break;

                if (ar_pci->oper_irq_mode == ATH10K_PCI_IRQ_LEGACY)
                        /* Fix potential race by repeating CORE_BASE writes */
                        ath10k_pci_enable_legacy_irq(ar);

                mdelay(10);
        } while (zx_time_get(ZX_CLOCK_MONOTONIC) < timeout);

        ath10k_pci_disable_and_clear_legacy_irq(ar);
        ath10k_pci_irq_msi_fw_mask(ar);

        if (val == 0xffffffff) {
                ath10k_err("failed to read device register, device is gone\n");
                return ZX_ERR_IO;
        }

        if (val & FW_IND_EVENT_PENDING) {
                ath10k_warn("device has crashed during init\n");
                return ZX_ERR_INTERNAL;
        }

        if (!(val & FW_IND_INITIALIZED)) {
                ath10k_err("failed to receive initialized event from target: %08x\n",
                           val);
                return ZX_ERR_TIMED_OUT;
        }

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot target initialised\n");
        return ZX_OK;
}

/* 3018 */
static zx_status_t ath10k_pci_cold_reset(struct ath10k *ar)
{
        uint32_t val;

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot cold reset\n");

        pthread_spin_lock(&ar->data_lock);

        ar->stats.fw_cold_reset_counter++;

        pthread_spin_unlock(&ar->data_lock);

        /* Put Target, including PCIe, into RESET. */
        val = ath10k_pci_reg_read32(ar, SOC_GLOBAL_RESET_ADDRESS);
        val |= 1;
        ath10k_pci_reg_write32(ar, SOC_GLOBAL_RESET_ADDRESS, val);

        /* After writing into SOC_GLOBAL_RESET to put device into
         * reset and pulling out of reset pcie may not be stable
         * for any immediate pcie register access and cause bus error,
         * add delay before any pcie access request to fix this issue.
         */
	zx_nanosleep(zx_deadline_after(ZX_MSEC(20)));

        /* Pull Target, including PCIe, out of RESET. */
        val &= ~1;
        ath10k_pci_reg_write32(ar, SOC_GLOBAL_RESET_ADDRESS, val);

	zx_nanosleep(zx_deadline_after(ZX_MSEC(20)));

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot cold reset complete\n");

        return ZX_OK;
}

/* 3053 */
static zx_status_t ath10k_pci_claim(struct ath10k *ar)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
        pci_protocol_t *pdev = &ar_pci->pdev;
        zx_status_t ret;

	ret = pci_map_resource(pdev, PCI_RESOURCE_BAR_0, ZX_CACHE_POLICY_UNCACHED_DEVICE,
			       &ar_pci->mem, &ar_pci->mem_len, &ar_pci->mem_handle);
        if (ret != ZX_OK) {
		ath10k_err("failed to map resources for BAR 0: %s\n",
                           zx_status_get_string(ret));
		goto err_device;
	}

	// Verify that the requested addresses are in 32b range
	zx_paddr_t phys_addr;
	ret = zx_vmo_op_range(ar_pci->mem_handle, ZX_VMO_OP_LOOKUP, 0, 8, &phys_addr,
			      sizeof(zx_paddr_t));
        if (ret != ZX_OK) {
        	ath10k_err("failed to get physical address of PCI mem\n");
		goto err_region;
	}
	if (phys_addr + ar_pci->mem_len > 0xffffffff) {
		ath10k_err("PCI mem allocated outside of 32-bit address space\n");
		ret = ZX_ERR_INTERNAL;
		goto err_region;
	}
	ret = pci_enable_bus_master(pdev, true);
	if (ret != ZX_OK) {
		ath10k_err("failed to enable bus mastering\n");
		goto err_region;
	}

        ath10k_dbg(ar, ATH10K_DBG_BOOT, "boot pci_mem 0x%p\n", ar_pci->mem);
        return ZX_OK;

err_region:
	zx_handle_close(ar_pci->mem_handle);

err_device:
        return ret;
}

/* 3134 */
static bool ath10k_pci_chip_is_supported(uint32_t dev_id, uint32_t chip_id)
{
        const struct ath10k_pci_supp_chip *supp_chip;
        unsigned long i;
        uint32_t rev_id = MS(chip_id, SOC_CHIP_ID_REV);

        for (i = 0; i < ARRAY_SIZE(ath10k_pci_supp_chips); i++) {
                supp_chip = &ath10k_pci_supp_chips[i];

                if (supp_chip->dev_id == dev_id &&
                    supp_chip->rev_id == rev_id)
                        return true;
        }

        return false;
}

/* 3142 */
zx_status_t ath10k_pci_setup_resource(struct ath10k *ar)
{
        struct ath10k_pci *ar_pci = ath10k_pci_priv(ar);
        zx_status_t ret;

	pthread_spin_init(&ar_pci->ce_lock, PTHREAD_PROCESS_PRIVATE);

#if 0 // TODO
        setup_timer(&ar_pci->rx_post_retry, ath10k_pci_rx_replenish_retry,
                    (unsigned long)ar);
#endif

        if (QCA_REV_6174(ar) || QCA_REV_9377(ar))
                ath10k_pci_override_ce_config(ar);

        ret = ath10k_pci_alloc_pipes(ar);
        if (ret) {
                ath10k_err("failed to allocate copy engine pipes: %s\n",
                           zx_status_get_string(ret));
                return ret;
        }
        return ZX_OK;
}

/* 3174 */
static const struct ath10k_bus_ops ath10k_pci_bus_ops = {
        .read32         = ath10k_bus_pci_read32,
        .write32        = ath10k_bus_pci_write32,
        .get_num_banks  = ath10k_pci_get_num_banks,
};

/* Fuchsia */
static void ath10k_pci_release(void* ctx) {
    struct ath10k* ath = ctx;
    // Clear mastering
    // Release handles
    free(ath);
}

/* Fuchsia */
static zx_protocol_device_t device_ops = {
    .version = DEVICE_OPS_VERSION,
    .release = ath10k_pci_release,
};

/* Fuchsia */
static zx_status_t ath10k_pci_query(void* ctx, uint32_t options, wlanmac_info_t* info) {
    return ZX_ERR_NOT_SUPPORTED;
}

/* Fuchsia */
static zx_status_t ath10k_pci_start(void* ctx, wlanmac_ifc_t* ifc, void* cookie) {
    return ZX_ERR_NOT_SUPPORTED;
}

/* Fuchsia */
static void ath10k_pci_stop(void* ctx) {
}

/* Fuchsia */
static zx_status_t ath10k_pci_queue_tx(void* ctx, uint32_t options, wlan_tx_packet_t* pkt) {
    return ZX_ERR_NOT_SUPPORTED;
}

/* Fuchsia */
static zx_status_t ath10k_pci_set_channel(void* ctx, uint32_t options, wlan_channel_t* chan) {
    return ZX_ERR_NOT_SUPPORTED;
}

/* Fuchsia */
static zx_status_t ath10k_pci_set_bss(void* ctx, uint32_t options, const uint8_t mac[6],
                                      uint8_t type) {
    return ZX_ERR_NOT_SUPPORTED;
}

/* Fuchsia */
static zx_status_t ath10k_pci_set_key(void* ctx, uint32_t options, wlan_key_config_t* key_config) {
    return ZX_ERR_NOT_SUPPORTED;
}

/* Fuchsia */
static wlanmac_protocol_ops_t wlanmac_ops = {
    .query = ath10k_pci_query,
    .start = ath10k_pci_start,
    .stop = ath10k_pci_stop,
    .queue_tx = ath10k_pci_queue_tx,
    .set_channel = ath10k_pci_set_channel,
    .set_bss = ath10k_pci_set_bss,
    .set_key = ath10k_pci_set_key,
};

/* 3180 */
static zx_status_t ath10k_pci_probe(void* ctx, zx_device_t* dev)
{
	zx_status_t ret = ZX_OK;
	struct ath10k *ar;
	struct ath10k_pci *ar_pci;
	enum ath10k_hw_rev hw_rev;
	uint32_t chip_id;
	bool pci_ps;
	zx_status_t (*pci_soft_reset)(struct ath10k *ar);
	zx_status_t (*pci_hard_reset)(struct ath10k *ar);
	uint32_t (*targ_cpu_to_ce_addr)(struct ath10k *ar, uint32_t addr);

	pci_protocol_t pci;
	if (device_get_protocol(dev, ZX_PROTOCOL_PCI, &pci) != ZX_OK) {
		return ZX_ERR_NOT_SUPPORTED;
	}

	zx_pcie_device_info_t pci_info;
	if (pci_get_device_info(&pci, &pci_info) != ZX_OK) {
		return ZX_ERR_NOT_SUPPORTED;
	}

	switch (pci_info.device_id) {
	case QCA988X_2_0_DEVICE_ID:
		hw_rev = ATH10K_HW_QCA988X;
		pci_ps = false;
		pci_soft_reset = ath10k_pci_warm_reset;
		pci_hard_reset = ath10k_pci_qca988x_chip_reset;
		targ_cpu_to_ce_addr = ath10k_pci_qca988x_targ_cpu_to_ce_addr;
		break;
	case QCA9887_1_0_DEVICE_ID:
		hw_rev = ATH10K_HW_QCA9887;
		pci_ps = false;
		pci_soft_reset = ath10k_pci_warm_reset;
		pci_hard_reset = ath10k_pci_qca988x_chip_reset;
		targ_cpu_to_ce_addr = ath10k_pci_qca988x_targ_cpu_to_ce_addr;
		break;
	case QCA6164_2_1_DEVICE_ID:
	case QCA6174_2_1_DEVICE_ID:
		hw_rev = ATH10K_HW_QCA6174;
		pci_ps = true;
		pci_soft_reset = ath10k_pci_warm_reset;
		pci_hard_reset = ath10k_pci_qca6174_chip_reset;
		targ_cpu_to_ce_addr = ath10k_pci_qca988x_targ_cpu_to_ce_addr;
		break;
	case QCA99X0_2_0_DEVICE_ID:
		hw_rev = ATH10K_HW_QCA99X0;
		pci_ps = false;
		pci_soft_reset = ath10k_pci_qca99x0_soft_chip_reset;
		pci_hard_reset = ath10k_pci_qca99x0_chip_reset;
		targ_cpu_to_ce_addr = ath10k_pci_qca99x0_targ_cpu_to_ce_addr;
		break;
	case QCA9984_1_0_DEVICE_ID:
		hw_rev = ATH10K_HW_QCA9984;
		pci_ps = false;
		pci_soft_reset = ath10k_pci_qca99x0_soft_chip_reset;
		pci_hard_reset = ath10k_pci_qca99x0_chip_reset;
		targ_cpu_to_ce_addr = ath10k_pci_qca99x0_targ_cpu_to_ce_addr;
		break;
	case QCA9888_2_0_DEVICE_ID:
		hw_rev = ATH10K_HW_QCA9888;
		pci_ps = false;
		pci_soft_reset = ath10k_pci_qca99x0_soft_chip_reset;
		pci_hard_reset = ath10k_pci_qca99x0_chip_reset;
		targ_cpu_to_ce_addr = ath10k_pci_qca99x0_targ_cpu_to_ce_addr;
		break;
	case QCA9377_1_0_DEVICE_ID:
		hw_rev = ATH10K_HW_QCA9377;
		pci_ps = true;
		pci_soft_reset = NULL;
		pci_hard_reset = ath10k_pci_qca6174_chip_reset;
		targ_cpu_to_ce_addr = ath10k_pci_qca988x_targ_cpu_to_ce_addr;
		break;
	default:
		ath10k_err("unrecognized device ID: %#0" PRIx16 "\n",
			   pci_info.device_id);
		return ZX_ERR_NOT_SUPPORTED;
	}

	ret = ath10k_core_create(&ar, sizeof(*ar_pci), dev, ATH10K_BUS_PCI,
				 hw_rev, &ath10k_pci_hif_ops);
	if (ret != ZX_OK) {
		printf("ath10k: failed to create core\n");
		return ret;
	}

	ath10k_info("pci probe %04x:%04x\n", pci_info.vendor_id, pci_info.device_id);

	ar_pci = ath10k_pci_priv(ar);
	memcpy(&ar_pci->pdev, &pci, sizeof(ar_pci->pdev));
	ar_pci->dev = dev;
	ar_pci->ar = ar;
	ar->dev_id = pci_info.device_id;
	ar_pci->pci_ps = pci_ps;
	ar_pci->bus_ops = &ath10k_pci_bus_ops;
	ar_pci->pci_soft_reset = pci_soft_reset;
	ar_pci->pci_hard_reset = pci_hard_reset;
	ar_pci->targ_cpu_to_ce_addr = targ_cpu_to_ce_addr;
	ar->id.vendor = pci_info.vendor_id;
	ar->id.device = pci_info.device_id;

	ret = ath10k_pci_setup_resource(ar);
	if (ret != ZX_OK) {
		ath10k_err("failed to setup resource: %s\n", zx_status_get_string(ret));
		goto err_core_destroy;
	}

	ret = ath10k_pci_claim(ar);
	if (ret != ZX_OK) {
		goto err_free_pipes;
	}

#if 0
	ret = ath10k_pci_force_wake(ar);
	if (ret) {
		ath10k_warn("failed to wake up device : %s\n", zx_status_get_string(ret));
		goto err_sleep;
	}
#endif

	ath10k_pci_ce_deinit(ar);
	ath10k_pci_irq_disable(ar);

	ret = ath10k_pci_init_irq(ar);
	if (ret != ZX_OK) {
		ath10k_err("failed to init irqs: %s\n", zx_status_get_string(ret));
		goto err_sleep;
	}

	ath10k_info("pci irq %s oper_irq_mode %d irq_mode %d reset_mode %d\n",
		    ath10k_pci_get_irq_method(ar), ar_pci->oper_irq_mode,
		    ath10k_pci_irq_mode, ath10k_pci_reset_mode);

	ret = ath10k_pci_request_irq(ar);
	if (ret) {
		ath10k_warn("failed to request irqs: %d\n", ret);
		goto err_deinit_irq;
	}

	ret = ath10k_pci_chip_reset(ar);
	if (ret) {
		ath10k_err("failed to reset chip: %s\n", zx_status_get_string(ret));
		goto err_free_irq;
	}

	chip_id = ath10k_pci_soc_read32(ar, SOC_CHIP_ID_ADDRESS);
	if (chip_id == 0xffffffff) {
		ath10k_err("failed to get chip id\n");
		goto err_free_irq;
	}

	if (!ath10k_pci_chip_is_supported(pci_info.device_id, chip_id)) {
		ath10k_err("device %04x with chip_id %08x isn't supported\n",
			   pci_info.device_id, chip_id);
		goto err_free_irq;
	}

	ret = ath10k_core_register(ar, chip_id);
	if (ret) {
		ath10k_err("failed to register driver core: %s\n", zx_status_get_string(ret));
		goto err_free_irq;
	}

        device_add_args_t args = {
            .version = DEVICE_ADD_ARGS_VERSION,
            .name = "ath10k",
            .ctx = ar,
            .ops = &device_ops,
            .proto_id = ZX_PROTOCOL_WLANMAC,
            .proto_ops = &wlanmac_ops,
        };

        ret = device_add(dev, &args, &ar->zxdev);
        if (ret != ZX_OK) {
            goto err_free_irq;
        }

	return ZX_OK;

err_free_irq:
	ath10k_pci_free_irq(ar);
	// ath10k_pci_rx_retry_sync(ar);

err_deinit_irq:
	ath10k_pci_deinit_irq(ar);

err_sleep:
	// ath10k_pci_sleep_sync(ar);
	ath10k_pci_release(ar);

err_free_pipes:
	ath10k_pci_free_pipes(ar);

err_core_destroy:
	ath10k_core_destroy(ar);

	return ret;
}

#if 0
/* Fuchsia */
static int init_thread(void* arg) {
    struct ath10k* ath = arg;
    zx_status_t ret = ath10k_start(ath);
    if (ret != ZX_OK) {
        return (int)ret;
    }
    return 0;
}
#endif

static zx_driver_ops_t ath10k_pci_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = ath10k_pci_probe,
};

ZIRCON_DRIVER_BEGIN(ath10k_pci, ath10k_pci_driver_ops, "zircon", "0.1", 10)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PCI),
    BI_ABORT_IF(NE, BIND_PCI_VID, ATHEROS_VID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA988X_2_0_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA6174_2_1_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA99X0_2_0_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA6164_2_1_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA9377_1_0_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA9984_1_0_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA9887_1_0_DEVICE_ID),
    BI_MATCH_IF(EQ, BIND_PCI_DID, QCA9888_2_0_DEVICE_ID),
ZIRCON_DRIVER_END(ath10k)
