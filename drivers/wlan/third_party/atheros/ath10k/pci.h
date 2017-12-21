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

#ifndef _PCI_H_
#define _PCI_H_

#include <ddk/protocol/pci.h>

#include "hw.h"
#include "ce.h"

/* 27 */
/*
 * maximum number of bytes that can be
 * handled atomically by DiagRead/DiagWrite
 */
#define DIAG_TRANSFER_LIMIT 2048

/* 33 */
struct bmi_xfer {
        bool tx_done;
        bool rx_done;
        bool wait_for_resp;
        uint32_t resp_len;
};

/* 40 */
/*
 * PCI-specific Target state
 *
 * NOTE: Structure is shared between Host software and Target firmware!
 *
 * Much of this may be of interest to the Host so
 * HOST_INTEREST->hi_interconnect_state points here
 * (and all members are 32-bit quantities in order to
 * facilitate Host access). In particular, Host software is
 * required to initialize pipe_cfg_addr and svc_to_pipe_map.
 */
struct pcie_state {
        /* Pipe configuration Target address */
        /* NB: ce_pipe_config[CE_COUNT] */
        uint32_t pipe_cfg_addr;

        /* Service to pipe map Target address */
        /* NB: service_to_pipe[PIPE_TO_CE_MAP_CN] */
        uint32_t svc_to_pipe_map;

        /* number of MSI interrupts requested */
        uint32_t msi_requested;

        /* number of MSI interrupts granted */
        uint32_t msi_granted;

        /* Message Signalled Interrupt address */
        uint32_t msi_addr;

        /* Base data */
        uint32_t msi_data;

        /*
         * Data for firmware interrupt;
         * MSI data for other interrupts are
         * in various SoC registers
         */
        uint32_t msi_fw_intr_data;

        /* PCIE_PWR_METHOD_* */
        uint32_t power_mgmt_method;

        /* PCIE_CONFIG_FLAG_* */
        uint32_t config_flags;
};

/* 86 */
/* PCIE_CONFIG_FLAG definitions */
#define PCIE_CONFIG_FLAG_ENABLE_L1  0x0000001

/* 89 */
/* Host software's Copy Engine configuration. */
#define CE_ATTR_FLAGS 0

/* 92 */
/*
 * Configuration information for a Copy Engine pipe.
 * Passed from Host to Target during startup (one per CE).
 *
 * NOTE: Structure is shared between Host software and Target firmware!
 */
struct ce_pipe_config {
        uint32_t pipenum;
        uint32_t pipedir;
        uint32_t nentries;
        uint32_t nbytes_max;
        uint32_t flags;
        uint32_t reserved;
};

/* 107 */
/*
 * Directions for interconnect pipe configuration.
 * These definitions may be used during configuration and are shared
 * between Host and Target.
 *
 * Pipe Directions are relative to the Host, so PIPEDIR_IN means
 * "coming IN over air through Target to Host" as with a WiFi Rx operation.
 * Conversely, PIPEDIR_OUT means "going OUT from Host through Target over air"
 * as with a WiFi Tx operation. This is somewhat awkward for the "middle-man"
 * Target since things that are "PIPEDIR_OUT" are coming IN to the Target
 * over the interconnect.
 */
#define PIPEDIR_NONE    0
#define PIPEDIR_IN      1  /* Target-->Host, WiFi Rx direction */
#define PIPEDIR_OUT     2  /* Host->Target, WiFi Tx direction */
#define PIPEDIR_INOUT   3  /* bidirectional */

/* Establish a mapping between a service/direction and a pipe. */
struct service_to_pipe {
        uint32_t service_id;
        uint32_t pipedir;
        uint32_t pipenum;
};

/* Per-pipe state. */
struct ath10k_pci_pipe {
        /* Handle of underlying Copy Engine */
        struct ath10k_ce_pipe *ce_hdl;

        /* Our pipe number; facilitiates use of pipe_info ptrs. */
        uint8_t pipe_num;

        /* Convenience back pointer to hif_ce_state. */
        struct ath10k *hif_ce_state;

        size_t buf_sz;

        /* protects compl_free and num_send_allowed */
        pthread_spinlock_t pipe_lock;
};

/* 148 */
struct ath10k_pci_supp_chip {
        uint32_t dev_id;
        uint32_t rev_id;
};

/* 153 */
struct ath10k_bus_ops {
        uint32_t (*read32)(struct ath10k *ar, uint32_t offset);
        void (*write32)(struct ath10k *ar, uint32_t offset, uint32_t value);
        int (*get_num_banks)(struct ath10k *ar);
};

/* 159 */
enum ath10k_pci_irq_mode {
        ATH10K_PCI_IRQ_AUTO = 0,
        ATH10K_PCI_IRQ_LEGACY = 1,
        ATH10K_PCI_IRQ_MSI = 2,
};

/* 165 */
struct ath10k_pci {

	/* 136 */
        struct ath10k_pci_pipe pipe_info[CE_COUNT_MAX];

	/* 166 */
	pci_protocol_t pdev;
	zx_device_t *dev;
	struct ath10k *ar;
        void* mem;
	uint64_t mem_len;
        zx_handle_t mem_handle;

	/* 172 */
        /* Operating interrupt mode */
        enum ath10k_pci_irq_mode oper_irq_mode;

	/* Fuchsia */
	zx_handle_t irq_handle;

        /* 177 */
	/* Copy Engine used for Diagnostic Accesses */
        struct ath10k_ce_pipe *ce_diag;

	/* 180 */
        /* FIXME: document what this really protects */
        pthread_spinlock_t ce_lock;

	/* 183 */
        /* Map CE id to ce_state */
        struct ath10k_ce_pipe ce_states[CE_COUNT_MAX];

	/* 222 */
        /* pci power save, disable for QCA988X and QCA99X0.
         * Writing 'false' to this variable avoids frequent locking
         * on MMIO read/write.
         */
        bool pci_ps;

	const struct ath10k_bus_ops *bus_ops;

        /* Chip specific pci reset routine used to do a safe reset */
        int (*pci_soft_reset)(struct ath10k *ar);

        /* Chip specific pci full reset function */
        int (*pci_hard_reset)(struct ath10k *ar);

        /* chip specific methods for converting target CPU virtual address
         * space to CE address space
         */
        uint32_t (*targ_cpu_to_ce_addr)(struct ath10k *ar, uint32_t addr);
};

/* 236 */
static inline struct ath10k_pci *ath10k_pci_priv(struct ath10k *ar)
{
	return (struct ath10k_pci *)ar->drv_priv;
}

/* 246 */
#define BAR_NUM 0

/* 263 */
/* Wait up to this many Ms for a Diagnostic Access CE operation to complete */
#define DIAG_ACCESS_CE_TIMEOUT_MS 10

/* 266 */
void ath10k_pci_write32(struct ath10k *ar, uint32_t offset, uint32_t value);

/* 270 */
uint32_t ath10k_pci_read32(struct ath10k *ar, uint32_t offset);

/* 278 */
zx_status_t ath10k_pci_diag_write_mem(struct ath10k *ar, uint32_t address,
	                              const void *data, int nbytes);

/* 291 */
void ath10k_pci_free_pipes(struct ath10k *ar);

/* 297 */
zx_status_t ath10k_pci_init_config(struct ath10k *ar);

/* 300 */
void ath10k_pci_enable_legacy_irq(struct ath10k *ar);

/* 302 */
void ath10k_pci_disable_and_clear_legacy_irq(struct ath10k *ar);
void ath10k_pci_irq_msi_fw_mask(struct ath10k *ar);
zx_status_t ath10k_pci_wait_for_target_init(struct ath10k *ar);

#endif /* _PCI_H_ */
