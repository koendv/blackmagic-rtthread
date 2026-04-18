/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2026 1BitSquared <info@1bitsquared.com>
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
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLEs
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "general.h"
#include "jtag_scan.h"
#include "jtagtap.h"
#include "target.h"
#include "target_internal.h"
#include "lattice_isp_clock.h"

/* Instructions for the ispCLOCK that we care about */
#define IR_ADDRESS_SHIFT    0x01U
#define IR_DATA_SHIFT       0x02U
#define IR_BULK_ERASE       0x03U
#define IR_PROGRAM          0x07U
#define IR_PROGRAM_SECURITY 0x09U
#define IR_VERIFY           0x0aU
#define IR_DISCHARGE        0x14U
#define IR_PROGRAM_ENABLE   0x15U
#define IR_USERCODE         0x17U
#define IR_PROGRAM_USERCODE 0x1aU
#define IR_PROGRAM_DISABLE  0x1eU
#define IR_ERASE_DONE       0x24U
#define IR_PROGRAM_INCR     0x27U
#define IR_VERIFY_INCR      0x2aU
#define IR_PROGRAM_DONE     0x2fU

#define ISP_CLOCK_NVM_BASE 0x00000000U
#define ISP_CLOCK_NVM_SIZE 0x00001500U

typedef struct isp_clock {
	uint8_t dev_index;
} isp_clock_s;

static bool isp_clock_enter_flash_mode(target_s *target);
static bool isp_clock_exit_flash_mode(target_s *target);
static bool isp_clock_flash_write(target_flash_s *flash, target_addr_t dest, const void *buffer, size_t lenth);
static bool isp_clock_flash_mass_erase(target_flash_s *flash, platform_timeout_s *print_progress);

static void isp_clock_add_flash(target_s *const target)
{
	target_flash_s *flash = calloc(1, sizeof(*flash));
	if (!flash) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}

	/*
	 * The ispCLOCK device has a small NVM that we treat like it's Flash here.
	 * It's arranged in 42-bit blocks ("columns") with a 10-bit address - this gives
	 * a total of 5376 bytes of "E²CMOS" NVM.
	 */
	flash->start = ISP_CLOCK_NVM_BASE;
	flash->length = ISP_CLOCK_NVM_SIZE;
	flash->blocksize = ISP_CLOCK_NVM_SIZE;
	/* Have to write 4 blocks at a time to get an integer number of bytes */
	flash->writesize = 21U;
	flash->mass_erase = isp_clock_flash_mass_erase;
	flash->write = isp_clock_flash_write;
	flash->erased = 0xffU;
	target_add_flash(target, flash);
}

void lattice_isp_clock_handler(const uint8_t dev_index)
{
	target_s *target = target_new();
	target->driver = "Lattice";
	target->core = "ispCLOCK";

	isp_clock_s *priv = calloc(1, sizeof(*priv));
	if (!priv) { /* calloc failed: heap exhaustion */
		DEBUG_ERROR("calloc: failed in %s\n", __func__);
		return;
	}
	priv->dev_index = dev_index;
	target->priv = priv;
	target->priv_free = free;

	target->enter_flash_mode = isp_clock_enter_flash_mode;
	target->exit_flash_mode = isp_clock_exit_flash_mode;
	isp_clock_add_flash(target);
}

bool isp_clock_enter_flash_mode(target_s *const target)
{
	return true;
}

bool isp_clock_exit_flash_mode(target_s *const target)
{
	return true;
}

static bool isp_clock_flash_write(target_flash_s *flash, target_addr_t dest, const void *buffer, size_t length)
{
	return true;
}

static bool isp_clock_flash_mass_erase(target_flash_s *flash, platform_timeout_s *print_progress)
{
	return true;
}
