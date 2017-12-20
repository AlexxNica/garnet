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

#include <zircon/status.h>

#include "bmi.h"
#include "hif.h"
#include "debug.h"
#include "htc.h"
#include "hw.h"

/* 64 */
zx_status_t ath10k_bmi_get_target_info(struct ath10k *ar,
				       struct bmi_target_info *target_info)
{
        struct bmi_cmd cmd;
        union bmi_resp resp;
        uint32_t cmdlen = sizeof(cmd.id) + sizeof(cmd.get_target_info);
        uint32_t resplen = sizeof(resp.get_target_info);
        int ret;

        ath10k_dbg(ar, ATH10K_DBG_BMI, "bmi get target info\n");

        if (ar->bmi.done_sent) {
                ath10k_warn("BMI Get Target Info Command disallowed\n");
                return ZX_ERR_SHOULD_WAIT;
        }

        cmd.id = BMI_GET_TARGET_INFO;

        ret = ath10k_hif_exchange_bmi_msg(ar, &cmd, cmdlen, &resp, &resplen);
        if (ret) {
                ath10k_warn("unable to get target info from device: %s\n",
			    zx_status_get_string(ret));
                return ret;
        }

        if (resplen < sizeof(resp.get_target_info)) {
                ath10k_warn("invalid get_target_info response length (%d)\n",
                            resplen);
                return ZX_ERR_IO;
        }

        target_info->version = resp.get_target_info.version;
        target_info->type    = resp.get_target_info.type;

        return ZX_OK;
}
