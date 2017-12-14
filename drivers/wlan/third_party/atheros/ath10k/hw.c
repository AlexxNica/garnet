/*
 * Copyright (c) 2014-2015 Qualcomm Atheros, Inc.
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
#include "hw.h"

const struct ath10k_hw_regs qca988x_regs = {
	.rtc_soc_base_address		= 0x00004000,
	.rtc_wmac_base_address		= 0x00005000,
	.soc_core_base_address		= 0x00009000,
	.wlan_mac_base_address		= 0x00020000,
	.ce_wrapper_base_address	= 0x00057000,
	.ce0_base_address		= 0x00057400,
	.ce1_base_address		= 0x00057800,
	.ce2_base_address		= 0x00057c00,
	.ce3_base_address		= 0x00058000,
	.ce4_base_address		= 0x00058400,
	.ce5_base_address		= 0x00058800,
	.ce6_base_address		= 0x00058c00,
	.ce7_base_address		= 0x00059000,
	.soc_reset_control_si0_rst_mask	= 0x00000001,
	.soc_reset_control_ce_rst_mask	= 0x00040000,
	.soc_chip_id_address		= 0x000000ec,
	.scratch_3_address		= 0x00000030,
	.fw_indicator_address		= 0x00009030,
	.pcie_local_base_address	= 0x00080000,
	.ce_wrap_intr_sum_host_msi_lsb	= 0x00000008,
	.ce_wrap_intr_sum_host_msi_mask	= 0x0000ff00,
	.pcie_intr_fw_mask		= 0x00000400,
	.pcie_intr_ce_mask_all		= 0x0007f800,
	.pcie_intr_clr_address		= 0x00000014,
};

const struct ath10k_hw_regs qca6174_regs = {
	.rtc_soc_base_address			= 0x00000800,
	.rtc_wmac_base_address			= 0x00001000,
	.soc_core_base_address			= 0x0003a000,
	.wlan_mac_base_address			= 0x00010000,
	.ce_wrapper_base_address		= 0x00034000,
	.ce0_base_address			= 0x00034400,
	.ce1_base_address			= 0x00034800,
	.ce2_base_address			= 0x00034c00,
	.ce3_base_address			= 0x00035000,
	.ce4_base_address			= 0x00035400,
	.ce5_base_address			= 0x00035800,
	.ce6_base_address			= 0x00035c00,
	.ce7_base_address			= 0x00036000,
	.soc_reset_control_si0_rst_mask		= 0x00000000,
	.soc_reset_control_ce_rst_mask		= 0x00000001,
	.soc_chip_id_address			= 0x000000f0,
	.scratch_3_address			= 0x00000028,
	.fw_indicator_address			= 0x0003a028,
	.pcie_local_base_address		= 0x00080000,
	.ce_wrap_intr_sum_host_msi_lsb		= 0x00000008,
	.ce_wrap_intr_sum_host_msi_mask		= 0x0000ff00,
	.pcie_intr_fw_mask			= 0x00000400,
	.pcie_intr_ce_mask_all			= 0x0007f800,
	.pcie_intr_clr_address			= 0x00000014,
	.cpu_pll_init_address			= 0x00404020,
	.cpu_speed_address			= 0x00404024,
	.core_clk_div_address			= 0x00404028,
};

const struct ath10k_hw_regs qca99x0_regs = {
	.rtc_soc_base_address			= 0x00080000,
	.rtc_wmac_base_address			= 0x00000000,
	.soc_core_base_address			= 0x00082000,
	.wlan_mac_base_address			= 0x00030000,
	.ce_wrapper_base_address		= 0x0004d000,
	.ce0_base_address			= 0x0004a000,
	.ce1_base_address			= 0x0004a400,
	.ce2_base_address			= 0x0004a800,
	.ce3_base_address			= 0x0004ac00,
	.ce4_base_address			= 0x0004b000,
	.ce5_base_address			= 0x0004b400,
	.ce6_base_address			= 0x0004b800,
	.ce7_base_address			= 0x0004bc00,
	/* Note: qca99x0 supports upto 12 Copy Engines. Other than address of
	 * CE0 and CE1 no other copy engine is directly referred in the code.
	 * It is not really necessary to assign address for newly supported
	 * CEs in this address table.
	 *	Copy Engine		Address
	 *	CE8			0x0004c000
	 *	CE9			0x0004c400
	 *	CE10			0x0004c800
	 *	CE11			0x0004cc00
	 */
	.soc_reset_control_si0_rst_mask		= 0x00000001,
	.soc_reset_control_ce_rst_mask		= 0x00000100,
	.soc_chip_id_address			= 0x000000ec,
	.scratch_3_address			= 0x00040050,
	.fw_indicator_address			= 0x00040050,
	.pcie_local_base_address		= 0x00000000,
	.ce_wrap_intr_sum_host_msi_lsb		= 0x0000000c,
	.ce_wrap_intr_sum_host_msi_mask		= 0x00fff000,
	.pcie_intr_fw_mask			= 0x00100000,
	.pcie_intr_ce_mask_all			= 0x000fff00,
	.pcie_intr_clr_address			= 0x00000010,
};

const struct ath10k_hw_regs qca4019_regs = {
	.rtc_soc_base_address                   = 0x00080000,
	.soc_core_base_address                  = 0x00082000,
	.wlan_mac_base_address                  = 0x00030000,
	.ce_wrapper_base_address                = 0x0004d000,
	.ce0_base_address                       = 0x0004a000,
	.ce1_base_address                       = 0x0004a400,
	.ce2_base_address                       = 0x0004a800,
	.ce3_base_address                       = 0x0004ac00,
	.ce4_base_address                       = 0x0004b000,
	.ce5_base_address                       = 0x0004b400,
	.ce6_base_address                       = 0x0004b800,
	.ce7_base_address                       = 0x0004bc00,
	/* qca4019 supports upto 12 copy engines. Since base address
	 * of ce8 to ce11 are not directly referred in the code,
	 * no need have them in separate members in this table.
	 *      Copy Engine             Address
	 *      CE8                     0x0004c000
	 *      CE9                     0x0004c400
	 *      CE10                    0x0004c800
	 *      CE11                    0x0004cc00
	 */
	.soc_reset_control_si0_rst_mask         = 0x00000001,
	.soc_reset_control_ce_rst_mask          = 0x00000100,
	.soc_chip_id_address                    = 0x000000ec,
	.fw_indicator_address                   = 0x0004f00c,
	.ce_wrap_intr_sum_host_msi_lsb          = 0x0000000c,
	.ce_wrap_intr_sum_host_msi_mask         = 0x00fff000,
	.pcie_intr_fw_mask                      = 0x00100000,
	.pcie_intr_ce_mask_all                  = 0x000fff00,
	.pcie_intr_clr_address                  = 0x00000010,
};

const struct ath10k_hw_values qca988x_values = {
        .rtc_state_val_on               = 3,
        .ce_count                       = 8,
        .msi_assign_ce_max              = 7,
        .num_target_ce_config_wlan      = 7,
        .ce_desc_meta_data_mask         = 0xFFFC,
        .ce_desc_meta_data_lsb          = 2,
};

const struct ath10k_hw_values qca6174_values = {
        .rtc_state_val_on               = 3,
        .ce_count                       = 8,
        .msi_assign_ce_max              = 7,
        .num_target_ce_config_wlan      = 7,
        .ce_desc_meta_data_mask         = 0xFFFC,
        .ce_desc_meta_data_lsb          = 2,
};

const struct ath10k_hw_values qca99x0_values = {
        .rtc_state_val_on               = 5,
        .ce_count                       = 12,
        .msi_assign_ce_max              = 12,
        .num_target_ce_config_wlan      = 10,
        .ce_desc_meta_data_mask         = 0xFFF0,
        .ce_desc_meta_data_lsb          = 4,
};

const struct ath10k_hw_values qca9888_values = {
        .rtc_state_val_on               = 3,
        .ce_count                       = 12,
        .msi_assign_ce_max              = 12,
        .num_target_ce_config_wlan      = 10,
        .ce_desc_meta_data_mask         = 0xFFF0,
        .ce_desc_meta_data_lsb          = 4,
};

const struct ath10k_hw_values qca4019_values = {
        .ce_count                       = 12,
        .num_target_ce_config_wlan      = 10,
        .ce_desc_meta_data_mask         = 0xFFF0,
        .ce_desc_meta_data_lsb          = 4,
};

static struct ath10k_hw_ce_regs_addr_map qcax_src_ring = {
        .msb    = 0x00000010,
        .lsb    = 0x00000010,
        .mask   = GENMASK(16, 16),
};

static struct ath10k_hw_ce_regs_addr_map qcax_dst_ring = {
        .msb    = 0x00000011,
        .lsb    = 0x00000011,
        .mask   = GENMASK(17, 17),
};

static struct ath10k_hw_ce_regs_addr_map qcax_dmax = {
        .msb    = 0x0000000f,
        .lsb    = 0x00000000,
        .mask   = GENMASK(15, 0),
};

static struct ath10k_hw_ce_ctrl1 qcax_ctrl1 = {
        .addr           = 0x00000010,
        .hw_mask        = 0x0007ffff,
        .sw_mask        = 0x0007ffff,
        .hw_wr_mask     = 0x00000000,
        .sw_wr_mask     = 0x0007ffff,
        .reset_mask     = 0xffffffff,
        .reset          = 0x00000080,
        .src_ring       = &qcax_src_ring,
        .dst_ring       = &qcax_dst_ring,
        .dmax           = &qcax_dmax,
};

static struct ath10k_hw_ce_regs_addr_map qcax_cmd_halt_status = {
        .msb    = 0x00000003,
        .lsb    = 0x00000003,
        .mask   = GENMASK(3, 3),
};

static struct ath10k_hw_ce_cmd_halt qcax_cmd_halt = {
        .msb            = 0x00000000,
        .mask           = GENMASK(0, 0),
        .status_reset   = 0x00000000,
        .status         = &qcax_cmd_halt_status,
};

static struct ath10k_hw_ce_regs_addr_map qcax_host_ie_cc = {
        .msb    = 0x00000000,
        .lsb    = 0x00000000,
        .mask   = GENMASK(0, 0),
};

static struct ath10k_hw_ce_host_ie qcax_host_ie = {
        .copy_complete_reset    = 0x00000000,
        .copy_complete          = &qcax_host_ie_cc,
};

static struct ath10k_hw_ce_host_wm_regs qcax_wm_reg = {
        .dstr_lmask     = 0x00000010,
        .dstr_hmask     = 0x00000008,
        .srcr_lmask     = 0x00000004,
        .srcr_hmask     = 0x00000002,
        .cc_mask        = 0x00000001,
        .wm_mask        = 0x0000001E,
        .addr           = 0x00000030,
};

static struct ath10k_hw_ce_misc_regs qcax_misc_reg = {
        .axi_err        = 0x00000400,
        .dstr_add_err   = 0x00000200,
        .srcr_len_err   = 0x00000100,
        .dstr_mlen_vio  = 0x00000080,
        .dstr_overflow  = 0x00000040,
        .srcr_overflow  = 0x00000020,
        .err_mask       = 0x000007E0,
        .addr           = 0x00000038,
};

static struct ath10k_hw_ce_regs_addr_map qcax_src_wm_low = {
        .msb    = 0x0000001f,
        .lsb    = 0x00000010,
        .mask   = GENMASK(31, 16),
};

static struct ath10k_hw_ce_regs_addr_map qcax_src_wm_high = {
        .msb    = 0x0000000f,
        .lsb    = 0x00000000,
        .mask   = GENMASK(15, 0),
};

static struct ath10k_hw_ce_dst_src_wm_regs qcax_wm_src_ring = {
        .addr           = 0x0000004c,
        .low_rst        = 0x00000000,
        .high_rst       = 0x00000000,
        .wm_low         = &qcax_src_wm_low,
        .wm_high        = &qcax_src_wm_high,
};

static struct ath10k_hw_ce_regs_addr_map qcax_dst_wm_low = {
        .lsb    = 0x00000010,
        .mask   = GENMASK(31, 16),
};

static struct ath10k_hw_ce_regs_addr_map qcax_dst_wm_high = {
        .msb    = 0x0000000f,
        .lsb    = 0x00000000,
        .mask   = GENMASK(15, 0),
};

static struct ath10k_hw_ce_dst_src_wm_regs qcax_wm_dst_ring = {
        .addr           = 0x00000050,
        .low_rst        = 0x00000000,
        .high_rst       = 0x00000000,
        .wm_low         = &qcax_dst_wm_low,
        .wm_high        = &qcax_dst_wm_high,
};

struct ath10k_hw_ce_regs qcax_ce_regs = {
        .sr_base_addr           = 0x00000000,
        .sr_size_addr           = 0x00000004,
        .dr_base_addr           = 0x00000008,
        .dr_size_addr           = 0x0000000c,
        .ce_cmd_addr            = 0x00000018,
        .misc_ie_addr           = 0x00000034,
        .sr_wr_index_addr       = 0x0000003c,
        .dst_wr_index_addr      = 0x00000040,
        .current_srri_addr      = 0x00000044,
        .current_drri_addr      = 0x00000048,
        .host_ie_addr           = 0x0000002c,
        .ctrl1_regs             = &qcax_ctrl1,
        .cmd_halt               = &qcax_cmd_halt,
        .host_ie                = &qcax_host_ie,
        .wm_regs                = &qcax_wm_reg,
        .misc_regs              = &qcax_misc_reg,
        .wm_srcr                = &qcax_wm_src_ring,
        .wm_dstr                = &qcax_wm_dst_ring,
};

