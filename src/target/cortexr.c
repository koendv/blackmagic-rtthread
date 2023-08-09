/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2023 1BitSquared <info@1bitsquared.com>
 * Written by Rachel Mant <git@dragonmux.network>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "general.h"
#include "adiv5.h"
#include "target.h"
#include "target_internal.h"
#include "target_probe.h"
#include "jep106.h"
#include "cortex.h"
#include "cortex_internal.h"

typedef struct cortexr_priv {
	/* Base core information */
	cortex_priv_s base;
} cortexr_priv_s;

#define CORTEXR_DBG_WFAR  0x018U
#define CORTEXR_DBG_VCR   0x01cU
#define CORTEXR_DBG_DSCCR 0x028U
#define CORTEXR_DBG_DTRTX 0x080U
#define CORTEXR_DBG_ITR   0x084U
#define CORTEXR_DBG_DSCR  0x088U
#define CORTEXR_DBG_DTRRX 0x08cU
#define CORTEXR_DBG_DRCR  0x090U
#define CORTEXR_DBG_BVR   0x100U
#define CORTEXR_DBG_BCR   0x140U
#define CORTEXR_DBG_WVR   0x180U
#define CORTEXR_DBG_WCR   0x1c0U

#define CORTEXR_CPUID 0xd00U
#define CORTEXR_CTR   0xd04U

static void cortexr_mem_read(target_s *const target, void *const dest, const target_addr_t src, const size_t len)
{
	adiv5_mem_read(cortex_ap(target), dest, src, len);
}

static void cortexr_mem_write(target_s *const target, const target_addr_t dest, const void *const src, const size_t len)
{
	adiv5_mem_write(cortex_ap(target), dest, src, len);
}

bool cortexr_probe(adiv5_access_port_s *const ap, const target_addr_t base_address)
{
	target_s *target = target_new();
	if (!target)
		return false;

	adiv5_ap_ref(ap);
	if (ap->dp->version >= 2 && ap->dp->target_designer_code != 0) {
		/* Use TARGETID register to identify target */
		target->designer_code = ap->dp->target_designer_code;
		target->part_id = ap->dp->target_partno;
	} else {
		/* Use AP DESIGNER and AP PARTNO to identify target */
		target->designer_code = ap->designer_code;
		target->part_id = ap->partno;
	}

	cortexr_priv_s *const priv = calloc(1, sizeof(*priv));
	if (!priv) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return false;
	}

	target->priv = priv;
	target->priv_free = cortex_priv_free;
	priv->base.ap = ap;
	priv->base.base_addr = base_address;

	target->check_error = cortex_check_error;
	target->mem_read = cortexr_mem_read;
	target->mem_write = cortexr_mem_write;

	target->driver = "ARM Cortex-R";

	cortex_read_cpuid(target);

	return true;
}