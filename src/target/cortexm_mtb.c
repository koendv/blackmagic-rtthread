/*
 * support for the CoreSight Micro Trace Buffer (MTB). 
 * reference: ARM DDI 0486B CoreSight MTB-M0+ TRM
 */

#include <string.h>

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "target_probe.h"
#include "adiv5.h"
#include "cortex.h"
#include "cortexm.h"
#include "buffer_utils.h"

/* table 3-1, section 3.3.1 */
#define CORTEXM_MTB_POSITION_OFFSET 0x000U
#define CORTEXM_MTB_MASTER_OFFSET   0x004U
#define CORTEXM_MTB_FLOW_OFFSET     0x008U
#define CORTEXM_MTB_BASE_OFFSET     0x00cU

/* table 3-4, section 3.4.2 */
#define CORTEXM_MTB_MASTER_EN        (1U << 31U)
#define CORTEXM_MTB_MASTER_HALTREQ   (1U << 9U)
#define CORTEXM_MTB_MASTER_MASK_MASK 0x1fU

/* table 3-3, section 3.4.1 */
#define CORTEXM_MTB_POSITION_POINTER_MASK 0xfffffff8U
#define CORTEXM_MTB_POSITION_WRAP         (1U << 2U)

/* table 3-6, section 3.4.3 */
#define CORTEXM_MTB_FLOW_WATERMARK_MASK 0xfffffff8U
#define CORTEXM_MTB_FLOW_AUTOHALT       (1U << 1U)
#define CORTEXM_MTB_FLOW_AUTOSTOP       (1U << 0U)

/* section 2.3.1, figure 2-4 */
#define CORTEXM_MTB_PACKET_ADDR_MASK 0xfffffffeU

#define CORTEXM_MTB_DUMP_CHUNK_SIZE 256U

#define CORTEXM_MTB_LPC84X_SFR_BASE  0x5000c000U
#define CORTEXM_MTB_LPC84X_SRAM_BASE 0x10000000U

static void cortexm_mtb_fixup(target_s *const target, adiv5_access_port_s *const ap)
{
	if (target->driver && (!strcmp(target->driver, "LPC844") || !strcmp(target->driver, "LPC845"))) {
		ap->mtb_base = CORTEXM_MTB_LPC84X_SFR_BASE;
		ap->mtb_sram = CORTEXM_MTB_LPC84X_SRAM_BASE;
	}
}

static bool cortexm_mtb_resolve(target_s *const target, adiv5_access_port_s *const ap)
{
	if (ap->mtb_sram)
		return true;

	cortexm_mtb_fixup(target, ap);

	if (!ap->mtb_base)
		return false;
	if (ap->mtb_sram)
		return true;

	uint32_t sram_base = 0;
	if (target_mem32_read(target, &sram_base, ap->mtb_base + CORTEXM_MTB_BASE_OFFSET, sizeof(sram_base))) {
		tc_printf(target, "mtb base read failed\n");
		return false;
	}
	ap->mtb_sram = sram_base;
	return ap->mtb_sram != 0U;
}

static bool cortexm_mtb_size(target_s *const target, const adiv5_access_port_s *const ap)
{
	/* section B.1: Discovery */
	bool result = false;

	uint32_t master = 0;
	if (target_mem32_read(target, &master, ap->mtb_base + CORTEXM_MTB_MASTER_OFFSET, sizeof(master))) {
		tc_printf(target, "mtb master read failed\n");
		return false;
	}

	uint32_t position = 0;
	if (target_mem32_read(target, &position, ap->mtb_base + CORTEXM_MTB_POSITION_OFFSET, sizeof(position))) {
		tc_printf(target, "mtb position read failed\n");
		return false;
	}

	/* stop tracing */
	const uint32_t master_off = master & ~CORTEXM_MTB_MASTER_EN;
	if (target_mem32_write(target, ap->mtb_base + CORTEXM_MTB_MASTER_OFFSET, &master_off, sizeof(master_off))) {
		tc_printf(target, "mtb master write failed\n");
		return false;
	}

	const uint32_t all_ones = CORTEXM_MTB_POSITION_POINTER_MASK;
	if (target_mem32_write(target, ap->mtb_base + CORTEXM_MTB_POSITION_OFFSET, &all_ones, sizeof(all_ones))) {
		tc_printf(target, "mtb position write failed\n");
		goto restore;
	}

	uint32_t position_readback = 0;
	if (target_mem32_read(
			target, &position_readback, ap->mtb_base + CORTEXM_MTB_POSITION_OFFSET, sizeof(position_readback))) {
		tc_printf(target, "mtb position readback failed\n");
		goto restore;
	}

	uint8_t bit_count = 0;
	for (uint32_t value = position_readback & CORTEXM_MTB_POSITION_POINTER_MASK; value; value >>= 1U)
		bit_count += value & 1U;

	/* section 1.5 table 1-1: AWIDTH is 5 to 32, so 2 to 29 pointer bits */
	if (bit_count < 2U || bit_count > 29U) {
		tc_printf(target, "mtb position write ignored\n");
		goto restore;
	}

	uint32_t awidth = bit_count + 3U;
	tc_printf(target, "max buffer size: %" PRIu64 " bytes\n", UINT64_C(1) << awidth);
	result = true;

restore:
	if (target_mem32_write(target, ap->mtb_base + CORTEXM_MTB_POSITION_OFFSET, &position, sizeof(position))) {
		/* pointer unknown: leave tracing off rather than write trace to an unknown address */
		tc_printf(target, "mtb position restore failed, trace left disabled\n");
		return false;
	}

	if (target_mem32_write(target, ap->mtb_base + CORTEXM_MTB_MASTER_OFFSET, &master, sizeof(master))) {
		tc_printf(target, "mtb master restore failed, trace left disabled\n");
		return false;
	}

	return result;
}

static uint32_t cortexm_mtb_buffer_size(const uint32_t master)
{
	return 1U << ((master & CORTEXM_MTB_MASTER_MASK_MASK) + 4U); /* section 3.4.2, 2^(mask+4) bytes */
}

static bool cortexm_mtb_dump(target_s *const target, const adiv5_access_port_s *const ap)
{
	uint32_t master = 0;
	if (target_mem32_read(target, &master, ap->mtb_base + CORTEXM_MTB_MASTER_OFFSET, sizeof(master))) {
		tc_printf(target, "mtb master read failed\n");
		return false;
	}

	uint32_t position = 0;
	if (target_mem32_read(target, &position, ap->mtb_base + CORTEXM_MTB_POSITION_OFFSET, sizeof(position))) {
		tc_printf(target, "mtb position read failed\n");
		return false;
	}

	const uint32_t buffer_size = cortexm_mtb_buffer_size(master);
	const uint32_t raw_pointer = position & CORTEXM_MTB_POSITION_POINTER_MASK;
	const uint32_t buffer_offset = raw_pointer & ~(buffer_size - 1U);
	const uint32_t pointer = raw_pointer & (buffer_size - 1U);
	const bool wrapped = (position & CORTEXM_MTB_POSITION_WRAP) != 0U;
	const uint32_t start_offset = wrapped ? pointer : 0U;
	const uint32_t valid_bytes = wrapped ? buffer_size : pointer;

	if (!valid_bytes) {
		tc_printf(target, "empty trace\n");
		return true;
	}

	uint8_t chunk[CORTEXM_MTB_DUMP_CHUNK_SIZE];
	uint32_t offset = start_offset;
	uint32_t remaining = valid_bytes;
	while (remaining) {
		uint32_t amount = MIN(CORTEXM_MTB_DUMP_CHUNK_SIZE, remaining);
		amount = MIN(amount, buffer_size - offset);
		const target_addr_t chunk_addr = ap->mtb_sram + buffer_offset + offset;
		if (target_mem32_read(target, chunk, chunk_addr, amount)) {
			tc_printf(target, "mtb sram read failed\n");
			return false;
		}
		for (uint32_t idx = 0U; idx < amount; idx += 8U) {
			const uint32_t source = read_le4(chunk, idx);
			const uint32_t dest = read_le4(chunk, idx + 4U);
			const uint32_t source_addr = source & CORTEXM_MTB_PACKET_ADDR_MASK;
			const bool source_flag = source & 1U;
			const uint32_t dest_addr = dest & CORTEXM_MTB_PACKET_ADDR_MASK;
			const bool dest_flag = dest & 1U;
			tc_printf(target, "%c 0x%08" PRIx32 " %c 0x%08" PRIx32 "\n", source_flag ? 'A' : '-', source_addr,
				dest_flag ? 'S' : '-', dest_addr);
		}
		offset = (offset + amount) & (buffer_size - 1U);
		remaining -= amount;
	}
	return true;
}

static const char *on_or_off(const uint32_t value)
{
	return value ? "on" : "off";
}

static bool cortexm_mtb_status(target_s *const target, const adiv5_access_port_s *const ap)
{
	uint32_t master = 0;
	if (target_mem32_read(target, &master, ap->mtb_base + CORTEXM_MTB_MASTER_OFFSET, sizeof(master))) {
		tc_printf(target, "mtb master read failed\n");
		return false;
	}

	uint32_t position = 0;
	if (target_mem32_read(target, &position, ap->mtb_base + CORTEXM_MTB_POSITION_OFFSET, sizeof(position))) {
		tc_printf(target, "mtb position read failed\n");
		return false;
	}
	uint32_t flow = 0;
	if (target_mem32_read(target, &flow, ap->mtb_base + CORTEXM_MTB_FLOW_OFFSET, sizeof(flow))) {
		tc_printf(target, "mtb flow read failed\n");
		return false;
	}

	tc_printf(target, "trace: %s halt request: %s wrapped: %s autostop: %s autohalt: %s\n",
		on_or_off(master & CORTEXM_MTB_MASTER_EN), on_or_off(master & CORTEXM_MTB_MASTER_HALTREQ),
		(position & CORTEXM_MTB_POSITION_WRAP) ? "yes" : "no", on_or_off(flow & CORTEXM_MTB_FLOW_AUTOSTOP),
		on_or_off(flow & CORTEXM_MTB_FLOW_AUTOHALT));

	const uint32_t buffer_size = cortexm_mtb_buffer_size(master);
	const uint32_t raw_pointer = position & CORTEXM_MTB_POSITION_POINTER_MASK;
	const uint32_t buffer_offset = raw_pointer & ~(buffer_size - 1U);
	const target_addr_t buffer_base = ap->mtb_sram + buffer_offset;
	const target_addr_t watermark = ap->mtb_sram + (flow & CORTEXM_MTB_FLOW_WATERMARK_MASK);
	tc_printf(target, "buffer: 0x%08" PRIx32 " - 0x%08" PRIx32 " (%" PRIu32 " bytes) watermark: 0x%08" PRIx32 "\n",
		(uint32_t)buffer_base, (uint32_t)(buffer_base + buffer_size - 1U), buffer_size, (uint32_t)watermark);

	tc_printf(target, "mtb base: 0x%08" PRIx32 " sram base: 0x%08" PRIx32 "\n", (uint32_t)ap->mtb_base,
		(uint32_t)ap->mtb_sram);
	tc_printf(target, "mtb master: 0x%08" PRIx32 " position: 0x%08" PRIx32 " flow: 0x%08" PRIx32 "\n", master, position,
		flow);
	return true;
}

bool cortexm_mtb(target_s *const target, const int argc, const char **const argv)
{
	if (!target_is_cortexm(target)) {
		tc_printf(target, "not cortexm\n");
		return false;
	}

	adiv5_access_port_s *const ap = cortex_ap(target);
	if (!cortexm_mtb_resolve(target, ap)) {
		tc_printf(target, "no mtb\n");
		return false;
	}

	if (argc == 2 && !strncmp(argv[1], "status", strlen(argv[1]))) {
		return cortexm_mtb_status(target, ap);
	} else if (argc == 2 && !strncmp(argv[1], "dump", strlen(argv[1]))) {
		return cortexm_mtb_dump(target, ap);
	} else if (argc == 2 && !strncmp(argv[1], "size", strlen(argv[1]))) {
		return cortexm_mtb_size(target, ap);
	} else {
		tc_printf(target, "usage: mon mtb [status|dump|size]\n");
		return true;
	}
	return false;
}
