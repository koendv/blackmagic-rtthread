/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2020, 2026 Koen De Vleeschauwer
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* decoding of ITM and DWT over SWO */

#ifdef PLATFORM_RTTHREAD
#include <rtthread.h>
#include <rtconfig.h>
#endif

#include "general.h"
#include "platform.h"
#include "swo.h"

#ifdef PLATFORM_RTTHREAD
#define CALLOC rt_calloc
#define FREE   rt_free
#else
#define CALLOC calloc
#define FREE   free
#endif

#ifndef SWO_DECODE_SIZE
#ifdef RT_CHERRYUSB_DEVICE_SPEED_HS
#define SWO_DECODE_SIZE (511)
#else
#define SWO_DECODE_SIZE (63)
#endif
#endif

#define DWT_TOP_N               25U
#define DWT_TOP_LINE_PREFIX_LEN 16U
#define DWT_TOP_MAP_WIDTH       60U
#ifndef DWT_TOP_MAX_BUCKETS
#define DWT_TOP_MAX_BUCKETS 4096U
#endif

static uint8_t itm_decoded_buffer[SWO_DECODE_SIZE];
static uint16_t itm_decoded_buffer_index = 0;
static uint32_t itm_decode_mask = 0;

/* pwin[] and pwin_len are the state between two swo_itm_decode() calls */
static uint8_t pwin[7]; /* packet window */
static uint8_t pwin_len = 0;

static uint16_t *dwt_top_buckets = NULL;
static uint32_t dwt_top_num_buckets = 0;
static uint32_t dwt_top_last_ms = 0;

/* true while "swo top|graph" is active: accumulate stats instead of logging each packet */
static bool mode_top = false;

struct dwt_top_entry {
	uint32_t index;
	uint16_t count;
};

#define DWT_EXCEPTION_COUNT 512U
static uint16_t *dwt_exception_counts = NULL;

static bool dwt_overflow_flag = false;

/* current dwt top/graph configuration, kept in sync by swo_itm_decode_set_top() */
dwt_top_settings_t dwt_top = {0};

enum ts_format_e {
	TS_FORMAT_LT,
	TS_FORMAT_GT1,
	TS_FORMAT_GT2,
};

static uint64_t ts_value = 0;
static uint8_t ts_meta = 0;
static uint8_t ts_byte_count = 0;

#define ITM_HW_DISCRIMINATOR_DWT_EVENT       0U
#define ITM_HW_DISCRIMINATOR_EXCEPTION       1U
#define ITM_HW_DISCRIMINATOR_PC_SAMPLE       2U
#define ITM_HW_DISCRIMINATOR_DATAWATCH_FIRST 8U
#define ITM_HW_DISCRIMINATOR_DATAWATCH_LAST  23U

/* ARMv7-M exception names */
static const char *const dwt_exception_names[16] = {
	"None",
	"Reset",
	"NMI",
	"HardFault",
	"MemManage",
	"BusFault",
	"UsageFault",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved",
	"SVCall",
	"DebugMonitor",
	"Reserved",
	"PendSV",
	"SysTick",
};

static const char hex_digits[] = "0123456789ABCDEF";
static char pc_sample_line[] = "PC:0x00000000\n";
static const char pc_sample_sleep_line[] = "PC:SLEEP\n";
static const char pc_sample_denied_line[] = "PC:DENY\n";
static const char overflow_line[] = "OVF\n";
static char exception_line[] = "EXC?:0x000\n";
static char dwt_event_line[] = "EV:0x00\n";
static char lt_line[] = "LT0:0x0000000\n";
static char gt1_line[] = "GT10:0x0000000\n";
static char gt2_line[] = "GT2:0x0000000000\n";
static char datawatch_line[7U + 8U + 1U] = "DW  :0x";
static char dwt_top_line[33] = "      0x00000000";

static void itm_decoded_buffer_push(const uint8_t *data, uint16_t len)
{
	memcpy(itm_decoded_buffer + itm_decoded_buffer_index, data, len);
	itm_decoded_buffer_index += len;
}

static inline void dwt_top_push_checked(const uint8_t *const data, const uint16_t len)
{
	if (itm_decoded_buffer_index + len > sizeof(itm_decoded_buffer)) {
		debug_serial_send_stdout(itm_decoded_buffer, itm_decoded_buffer_index);
		itm_decoded_buffer_index = 0U;
	}
	itm_decoded_buffer_push(data, len);
}

static inline void dwt_top_sample(const uint8_t *const payload)
{
	const uint32_t pc = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8U) | ((uint32_t)payload[2] << 16U) |
		((uint32_t)payload[3] << 24U);
	if (pc < dwt_top.low_addr)
		return;
	const uint32_t bucket = (pc - dwt_top.low_addr) >> dwt_top.bucket_bits;
	if (bucket >= dwt_top_num_buckets)
		return;
	if (dwt_top_buckets[bucket] < 0xFFFFU)
		++dwt_top_buckets[bucket];
}

/* ranks the non-zero entries of counts[0..num_counts) into the DWT_TOP_N largest, ascending by count */
static uint32_t dwt_top_rank(const uint16_t *const counts, const uint32_t num_counts, struct dwt_top_entry *const top)
{
	uint32_t filled = 0U;

	for (uint32_t index = 0; index < num_counts; ++index) {
		const uint16_t count = counts[index];
		if (count == 0U)
			continue;
		if (filled == DWT_TOP_N && count <= top[0].count)
			continue;

		uint32_t lo = 0U;
		uint32_t hi = filled;
		while (lo < hi) {
			const uint32_t mid = (lo + hi) / 2U;
			if (top[mid].count > count)
				hi = mid;
			else
				lo = mid + 1U;
		}

		if (filled < DWT_TOP_N) {
			for (uint32_t i = filled; i > lo; --i)
				top[i] = top[i - 1U];
			++filled;
		} else {
			for (uint32_t i = 0U; i < lo - 1U; ++i)
				top[i] = top[i + 1U];
			lo -= 1U;
		}
		top[lo].index = index;
		top[lo].count = count;
	}

	return filled;
}

/*
   the high-frequency logging path does not use snprint(),
   the low-frequency "top" path does use snprintf().
 */

static void dwt_top_dump(void)
{
	static struct dwt_top_entry top[DWT_TOP_N];

	const uint32_t filled = dwt_top_rank(dwt_top_buckets, dwt_top_num_buckets, top);

	if (itm_decoded_buffer_index) {
		debug_serial_send_stdout(itm_decoded_buffer, itm_decoded_buffer_index);
		itm_decoded_buffer_index = 0U;
	}

	if (dwt_top.graph) {
		static const char clear_home_sequence[] = "\x1b[2J\x1b[H";
		itm_decoded_buffer_push((const uint8_t *)clear_home_sequence, sizeof(clear_home_sequence) - 1U);
	}

	for (uint32_t i = filled; i > 0U; --i) {
		const uint32_t idx = i - 1U;
		const uint32_t addr = dwt_top.low_addr + (top[idx].index << dwt_top.bucket_bits);

		int total_len;
		if (dwt_top.graph) {
			const uint32_t base_column = DWT_TOP_LINE_PREFIX_LEN + 2U;
			const uint32_t column = dwt_top_num_buckets > 1U ?
				base_column + (top[idx].index * (DWT_TOP_MAP_WIDTH - 1U)) / (dwt_top_num_buckets - 1U) :
				base_column;
			total_len = snprintf(dwt_top_line, sizeof(dwt_top_line),
				"%5" PRIu16 " 0x%08" PRIX32 "\x1b[%" PRIu32 "G\x1b[7m \x1b[0m\r\n", top[idx].count, addr, column);
		} else
			total_len =
				snprintf(dwt_top_line, sizeof(dwt_top_line), "%5" PRIu16 " 0x%08" PRIX32 "\n", top[idx].count, addr);
		if (total_len >= (int)sizeof(dwt_top_line))
			total_len = sizeof(dwt_top_line) - 1;
		else if (total_len < 0)
			total_len = 0;
		dwt_top_push_checked((const uint8_t *)dwt_top_line, (uint16_t)total_len);
	}

	const uint32_t exception_filled = dwt_top_rank(dwt_exception_counts, DWT_EXCEPTION_COUNT, top);

	for (uint32_t i = exception_filled; i > 0U; --i) {
		const uint32_t idx = i - 1U;
		const uint32_t exception_number = top[idx].index;

		int total_len;
		if (exception_number < ARRAY_LENGTH(dwt_exception_names))
			total_len = snprintf(dwt_top_line, sizeof(dwt_top_line), "%5" PRIu16 " %s\n", top[idx].count,
				dwt_exception_names[exception_number]);
		else
			total_len = snprintf(dwt_top_line, sizeof(dwt_top_line), "%5" PRIu16 " IRQ %" PRIu32 "\n", top[idx].count,
				exception_number - 16U);
		if (total_len >= (int)sizeof(dwt_top_line))
			total_len = sizeof(dwt_top_line) - 1;
		else if (total_len < 0)
			total_len = 0;
		dwt_top_push_checked((const uint8_t *)dwt_top_line, (uint16_t)total_len);
	}

	memset(dwt_exception_counts, 0, (DWT_EXCEPTION_COUNT + dwt_top_num_buckets) * sizeof(*dwt_exception_counts));

	if (dwt_overflow_flag)
		dwt_top_push_checked((const uint8_t *)"OVERFLOW\n", 9U);
	dwt_overflow_flag = false;
}

static inline void dwt_top_poll(void)
{
	const uint32_t now = platform_time_ms();
	if ((now - dwt_top_last_ms) < dwt_top.interval_seconds * 1000U)
		return;
	dwt_top_last_ms = now;
	dwt_top_dump();
}

static inline void itm_lts1(void)
{
	if (mode_top)
		return;
	lt_line[2] = (char)('0' + ts_meta);
	for (uint8_t i = 0; i < 7U; ++i)
		lt_line[6U + i] = hex_digits[(ts_value >> ((6U - i) * 4U)) & 0xFU];
	itm_decoded_buffer_push((const uint8_t *)lt_line, sizeof(lt_line) - 1U);
}

/* LT format 2 (short) */
static inline void itm_lt2(const uint8_t header)
{
	ts_value = (header >> 4U) & 0x7U;
	ts_meta = 0U;
	itm_lts1();
}

static const char gt1_tc_letters[4] = "NTPB"; /* Mnemonic for what is delayed: None Timestamp Packet Both */

static inline void itm_gts1(void)
{
	if (mode_top)
		return;
	gt1_line[3] = gt1_tc_letters[ts_meta];
	for (uint8_t i = 0; i < 7U; ++i)
		gt1_line[7U + i] = hex_digits[(ts_value >> ((6U - i) * 4U)) & 0xFU];
	itm_decoded_buffer_push((const uint8_t *)gt1_line, sizeof(gt1_line) - 1U);
}

static inline void itm_gts2(void)
{
	if (mode_top)
		return;
	const bool is_64bit = ts_byte_count == 6U;
	const uint8_t hex_digit_count = is_64bit ? 10U : 6U;
	uint8_t pos = 6U;
	for (int8_t i = (int8_t)hex_digit_count - 1; i >= 0; --i)
		gt2_line[pos++] = hex_digits[(ts_value >> ((uint8_t)i * 4U)) & 0xFU];
	gt2_line[pos] = '\n';
	itm_decoded_buffer_push((const uint8_t *)gt2_line, pos + 1U);
}

static inline void dwt_datawatch(const uint8_t source_id, const uint8_t *const payload, const uint8_t payload_len)
{
	const uint8_t cmpn = (source_id >> 1U) & 0x3U;
	const bool is_data_value = (source_id >> 3U) == 2U;
	const bool sub_bit = (source_id & 0x1U) != 0U;
	const char kind = is_data_value ? (sub_bit ? 'W' : 'R') : (sub_bit ? 'A' : 'P');

	datawatch_line[2] = (char)('0' + cmpn);
	datawatch_line[3] = kind;
	uint8_t pos = 7U;
	for (int8_t i = (int8_t)payload_len - 1; i >= 0; --i) {
		datawatch_line[pos++] = hex_digits[payload[i] >> 4U];
		datawatch_line[pos++] = hex_digits[payload[i] & 0xFU];
	}
	datawatch_line[pos] = '\n';
	itm_decoded_buffer_push((const uint8_t *)datawatch_line, pos + 1U);
}

static inline void dwt_pc_sample(const uint8_t *const payload)
{
	for (uint8_t i = 0; i < 4U; ++i) {
		pc_sample_line[5U + i * 2U] = hex_digits[payload[3U - i] >> 4U];
		pc_sample_line[6U + i * 2U] = hex_digits[payload[3U - i] & 0xFU];
	}
	itm_decoded_buffer_push((const uint8_t *)pc_sample_line, sizeof(pc_sample_line) - 1U);
}

static inline void itm_overflow(void)
{
	if (mode_top) {
		dwt_overflow_flag = true;
		return;
	}
	itm_decoded_buffer_push((const uint8_t *)overflow_line, sizeof(overflow_line) - 1U);
}

static inline void dwt_pc_sleep(void)
{
	itm_decoded_buffer_push((const uint8_t *)pc_sample_sleep_line, sizeof(pc_sample_sleep_line) - 1U);
}

static inline void dwt_pc_deny(void)
{
	itm_decoded_buffer_push((const uint8_t *)pc_sample_denied_line, sizeof(pc_sample_denied_line) - 1U);
}

static inline void dwt_exception(const uint8_t *const payload)
{
	const uint16_t exception_number = ((payload[1] & 0x01U) << 8U) | payload[0];
	const uint8_t event_type = payload[1] >> 4U;

	if (mode_top) {
		if (event_type == 1U && dwt_exception_counts[exception_number] < 0xFFFFU)
			++dwt_exception_counts[exception_number];
		return;
	}

	exception_line[3] = event_type == 1U ? '<' : event_type == 2U ? '>' : event_type == 3U ? '=' : '?';
	exception_line[7] = hex_digits[(exception_number >> 8U) & 0xFU];
	exception_line[8] = hex_digits[(exception_number >> 4U) & 0xFU];
	exception_line[9] = hex_digits[exception_number & 0xFU];
	itm_decoded_buffer_push((const uint8_t *)exception_line, sizeof(exception_line) - 1U);
}

static inline void dwt_event(const uint8_t *const payload)
{
	if (mode_top)
		return;
	dwt_event_line[5] = hex_digits[payload[0] >> 4U];
	dwt_event_line[6] = hex_digits[payload[0] & 0xFU];
	itm_decoded_buffer_push((const uint8_t *)dwt_event_line, sizeof(dwt_event_line) - 1U);
}

/* returns total packet length, or 0 if header needs incremental scanning (LTS1/GTS1/GTS2) */
static uint8_t itm_packet_fixed_length(const uint8_t header)
{
	const uint8_t sh_size = header & 0x07U;
	if ((uint8_t)(sh_size - 1U) <= 2U)
		return 1U + (1U << (sh_size - 1U)); /* SWIT */
	if ((header & 0x03U) != 0U)
		return 1U + (1U << ((header & 0x03U) - 1U)); /* HW-source */

	/* header & 0x03 == 0: protocol packet family */
	const bool is_short_form = (header & 0x80U) == 0U;
	const bool is_long_form = (header & 0xC0U) == 0xC0U;
	const bool is_lts_header =
		(header & 0x0FU) == 0U && header != 0x00U && header != 0x70U && (is_short_form || is_long_form);
	const bool is_gts_header = (header & 0xDFU) == 0x94U;
	if ((is_lts_header && is_long_form) || is_gts_header)
		return 0U; /* LT format 1, GTS1, GTS2: variable length */
	return 1U;     /* Sync-zero, Overflow, LT format 2, reserved */
}

/* LTS1/GTS1/GTS2: pkt[0] is the header, pkt[1..len] are continuation bytes */
static void itm_decode_variable_packet(const uint8_t *const pkt, const uint8_t len)
{
	const uint8_t header = pkt[0];
	const bool is_gts_header = (header & 0xDFU) == 0x94U;
	enum ts_format_e format;
	uint8_t meta = 0U;
	if (is_gts_header)
		format = (header & 0x20U) ? TS_FORMAT_GT2 : TS_FORMAT_GT1;
	else {
		format = TS_FORMAT_LT;
		meta = (header >> 4U) & 0x3U;
	}

	uint64_t value = 0U;
	uint8_t shift = 0U;
	const uint8_t continuation_count = len - 1U;
	for (uint8_t i = 0; i < continuation_count; ++i) {
		const uint8_t byte = pkt[1U + i];
		if (format == TS_FORMAT_GT1 && i == 3U) {
			meta = (byte >> 5U) & 0x3U;
			value |= (uint64_t)(byte & 0x1FU) << shift;
		} else
			value |= (uint64_t)(byte & 0x7FU) << shift;
		shift += 7U;
	}

	ts_value = value;
	ts_meta = meta;
	switch (format) {
	case TS_FORMAT_LT:
		itm_lts1();
		break;
	case TS_FORMAT_GT1:
		itm_gts1();
		break;
	case TS_FORMAT_GT2:
		ts_byte_count = continuation_count;
		itm_gts2();
		break;
	}
}

static void itm_decode_packet(const uint8_t *const pkt, const uint8_t len)
{
	const uint8_t header = pkt[0];

	if (len == 1U) {
		if (header == 0x70U)
			itm_overflow();
		else if ((header & 0x8FU) == 0U && header != 0x00U)
			itm_lt2(header);
		/* else: Sync-zero byte or reserved header -- no-op */
		return;
	}

	const uint8_t sh_size = header & 0x07U;
	if ((uint8_t)(sh_size - 1U) <= 2U) {
		/* SWIT */
		const uint8_t source_id = header >> 3U;
		if (itm_decode_mask & (1U << source_id))
			itm_decoded_buffer_push(pkt + 1U, len - 1U);
		return;
	}

	/* HW-source packet */
	const uint8_t source_id = header >> 3U;
	const uint8_t payload_len = len - 1U;
	const uint8_t *const payload = pkt + 1U;
	switch (source_id) {
	case ITM_HW_DISCRIMINATOR_PC_SAMPLE:
		if (mode_top) {
			if (payload_len == 4U)
				dwt_top_sample(payload);
		} else if (payload_len == 4U)
			dwt_pc_sample(payload);
		else if (payload_len == 1U && payload[0] == 0x00U)
			dwt_pc_sleep();
		else if (payload_len == 1U && payload[0] == 0xFFU)
			dwt_pc_deny();
		break;
	case ITM_HW_DISCRIMINATOR_DWT_EVENT:
		if (payload_len == 1U)
			dwt_event(payload);
		break;
	case ITM_HW_DISCRIMINATOR_EXCEPTION:
		if (payload_len == 2U)
			dwt_exception(payload);
		break;
	case ITM_HW_DISCRIMINATOR_DATAWATCH_FIRST ... ITM_HW_DISCRIMINATOR_DATAWATCH_LAST:
		dwt_datawatch(source_id, payload, payload_len);
		break;
	default:
		break; /* reserved discriminator: discard */
	}
}

static void itm_decode_process(const uint8_t *data, uint16_t len)
{
	uint16_t idx = 0U;
	for (;;) {
		if (pwin_len == 0U) {
			if (idx >= len)
				return;
			/* reserves space for the largest packet-decode push */
			if (itm_decoded_buffer_index + (sizeof(gt2_line) - 1U) > sizeof(itm_decoded_buffer)) {
				debug_serial_send_stdout(itm_decoded_buffer, itm_decoded_buffer_index);
				itm_decoded_buffer_index = 0U;
			}
			pwin[pwin_len++] = data[idx++];
		}

		const uint8_t total_len = itm_packet_fixed_length(pwin[0]);
		if (total_len != 0U) {
			while (pwin_len < total_len && idx < len)
				pwin[pwin_len++] = data[idx++];
			if (pwin_len < total_len)
				return;
			itm_decode_packet(pwin, total_len);
			pwin_len = 0U;
			continue;
		}

		/* LT format 1 / GTS1 / GTS2: scan continuation bytes for the terminator, up to 6 of them */
		while (pwin_len < 7U) {
			if (idx >= len)
				return;
			pwin[pwin_len++] = data[idx++];
			if (!(pwin[pwin_len - 1U] & 0x80U))
				break;
		}
		itm_decode_variable_packet(pwin, pwin_len);
		pwin_len = 0U;
	}
}

uint16_t swo_itm_decode(const uint8_t *data, uint16_t len)
{
	itm_decode_process(data, len);

	if (mode_top)
		dwt_top_poll();

	if (itm_decoded_buffer_index) {
		debug_serial_send_stdout(itm_decoded_buffer, itm_decoded_buffer_index);
		itm_decoded_buffer_index = 0U;
	}

	return len;
}

void swo_itm_decode_set_mask(uint32_t mask)
{
	itm_decode_mask = mask;
}

dwt_top_error_e swo_itm_decode_set_top(const dwt_top_settings_t *const settings)
{
	if (dwt_exception_counts) {
		FREE(dwt_exception_counts);
		dwt_exception_counts = NULL;
	}
	dwt_top_buckets = NULL;
	dwt_top_num_buckets = 0U;
	mode_top = false;

	if (settings->low_addr == settings->high_addr) {
		dwt_top.low_addr = settings->low_addr;
		dwt_top.high_addr = settings->high_addr;
		return dwt_top_err_none;
	}

	if (settings->bucket_bits > 31U) {
		return dwt_top_err_bits;
	}

	const uint32_t mask = (1UL << settings->bucket_bits) - 1UL;
	const uint32_t aligned_low = settings->low_addr & ~mask;
	const uint32_t aligned_high = (settings->high_addr + mask) & ~mask;
	if (aligned_high <= aligned_low) {
		return dwt_top_err_range;
	}

	const uint32_t num_buckets = (aligned_high - aligned_low) >> settings->bucket_bits;
	if (num_buckets > DWT_TOP_MAX_BUCKETS) {
		return dwt_top_err_too_many_buckets;
	}

	/* single allocation: exception counters (fixed size) followed by PC-sample buckets */
	dwt_exception_counts = CALLOC(DWT_EXCEPTION_COUNT + num_buckets, sizeof(*dwt_exception_counts));
	if (!dwt_exception_counts) {
		return dwt_top_err_no_memory;
	}
	dwt_top_buckets = dwt_exception_counts + DWT_EXCEPTION_COUNT;

	dwt_top.low_addr = aligned_low;
	dwt_top.high_addr = settings->high_addr;
	dwt_top.bucket_bits = settings->bucket_bits;
	dwt_top.interval_seconds = settings->interval_seconds;
	dwt_top.graph = settings->graph;
	dwt_top_num_buckets = num_buckets;
	dwt_top_last_ms = platform_time_ms();
	mode_top = true;
	return dwt_top_err_none;
}

static void itm_decode_selftest_send(const char *str, uint16_t len)
{
	itm_decoded_buffer_push((const uint8_t *)str, len);
	debug_serial_send_stdout(itm_decoded_buffer, itm_decoded_buffer_index);
	itm_decoded_buffer_index = 0U;
}

static void itm_decode_selftest_sendstr(const char *str)
{
	itm_decode_selftest_send(str, strlen(str));
}

static bool itm_decode_selftest_matches(const char *const description)
{
	const size_t expected_len = strlen(description);
	const bool matches = itm_decoded_buffer_index == expected_len + 1U &&
		memcmp(itm_decoded_buffer, description, expected_len) == 0 && itm_decoded_buffer[expected_len] == '\n';
	itm_decoded_buffer_index = 0U;
	return matches;
}

void swo_itm_decode_selftest(void)
{
	static const struct {
		const char *description;
		const uint8_t bytes[10];
		uint8_t length;
	} vectors[] = {
		{"ABCDEF", {0x01, 'A', 0x02, 'B', 'C', 0x03, 'D', 'E', 'F', '\n'}, 10},
		{"PC:0xFF3F0F03", {0x17, 0x03, 0x0F, 0x3F, 0xFF}, 5},
		{"PC:SLEEP", {0x15, 0x00}, 2},
		{"EXC=:0x020", {0x0E, 0x20, 0x30}, 3},
		{"EV:0x2F", {0x05, 0x2F}, 2},
		{"DW3P:0xFF3F0F03", {0x77, 0x03, 0x0F, 0x3F, 0xFF}, 5},
		{"DW2A:0x0F03", {0x6E, 0x03, 0x0F}, 3},
		{"DW2W:0xFF3F0F03", {0xAF, 0x03, 0x0F, 0x3F, 0xFF}, 5},
		{"LT0:0x00000C9", {0xC0, 0xC9, 0x01}, 3},
		{"LT0:0x0000005", {0x50}, 1},
		{"GT1B:0x0011000", {0x94, 0x80, 0xA0, 0x84, 0x60}, 5},
		{"GT2:0x247A3D", {0xB4, 0xBD, 0xF4, 0x91, 0x01}, 5},
		{"GT2:0x3F40247A3D", {0xB4, 0xBD, 0xF4, 0x91, 0x81, 0xF4, 0x07}, 7},
		{"OVF", {0x70}, 1},

		/* Sync+LT0:0x0000005 */
		{"LT0:0x0000005", {0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x50}, 7},
	};

	char line[48];

	/* save mask and switch off mask */
	const uint32_t saved_mask = itm_decode_mask;
	itm_decode_mask = 0xFFFFFFFFU;

	/* save mode and switch off "top" mode */
	const dwt_top_settings_t saved_top = dwt_top;
	const dwt_top_settings_t disable_top = {0};
	swo_itm_decode_set_top(&disable_top);

	itm_decode_selftest_sendstr("DWT/SWO decoder self-test\n");

	uint32_t pass_count = 0U;
	uint32_t fail_count = 0U;
	for (size_t i = 0; i < ARRAY_LENGTH(vectors); ++i) {
		itm_decoded_buffer_index = 0U;
		itm_decode_process(vectors[i].bytes, vectors[i].length);
		const bool matches = itm_decode_selftest_matches(vectors[i].description);

		if (matches)
			++pass_count;
		else
			++fail_count;

		const int total_len =
			snprintf(line, sizeof(line), "%2zu: %s %s\n", i + 1U, matches ? "PASS" : "FAIL", vectors[i].description);
		itm_decode_selftest_send(line, (uint16_t)total_len);
	}

	const int total_len =
		snprintf(line, sizeof(line), "%" PRIu32 " passed, %" PRIu32 " failed\n", pass_count, fail_count);
	itm_decode_selftest_send(line, (uint16_t)total_len);

	itm_decode_selftest_sendstr("straddling test\n");

	uint32_t straddle_pass = 0U;
	uint32_t straddle_fail = 0U;
	for (size_t i = 0; i < ARRAY_LENGTH(vectors); ++i) {
		const uint8_t vec_len = vectors[i].length;
		if (vec_len < 2U)
			continue; /* nothing to split */

		bool vector_ok = true;
		for (uint8_t split = 1U; split < vec_len; ++split) {
			itm_decoded_buffer_index = 0U;
			pwin_len = 0U;
			itm_decode_process(vectors[i].bytes, split);
			itm_decode_process(vectors[i].bytes + split, vec_len - split);

			if (!itm_decode_selftest_matches(vectors[i].description)) {
				vector_ok = false;
				++straddle_fail;
				const int total_len2 =
					snprintf(line, sizeof(line), "%2zu@%u: FAIL %s\n", i + 1U, split, vectors[i].description);
				itm_decode_selftest_send(line, (uint16_t)total_len2);
			}
		}

		if (vector_ok) {
			++straddle_pass;
			const int total_len2 = snprintf(line, sizeof(line), "%2zu: PASS %s\n", i + 1U, vectors[i].description);
			itm_decode_selftest_send(line, (uint16_t)total_len2);
		}
	}

	const int straddle_total_len =
		snprintf(line, sizeof(line), "%" PRIu32 " passed, %" PRIu32 " failed\n", straddle_pass, straddle_fail);
	itm_decode_selftest_send(line, (uint16_t)straddle_total_len);

	/* restore saved mask */
	itm_decode_mask = saved_mask;

	/* restore saved mode */
	swo_itm_decode_set_top(&saved_top);
}
