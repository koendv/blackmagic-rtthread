/* 
 * measure swd/jtag throughput
 * time writing a buffer to target ram
 */

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "gdb_packet.h"
#include "platform.h"

#include <rtthread.h>

#define SPEEDTEST_DEFAULT_SIZE  1024U /* bytes per write */
#define SPEEDTEST_MAX_SIZE      4096U
#define SPEEDTEST_DEFAULT_COUNT 16U

bool cmd_speedtest(target_s *const target, const int argc, const char **const argv)
{
	if (!target) {
		gdb_out("not attached\n");
		return false;
	}

	if (!target->ram) {
		tc_printf(target, "no ram\n");
		return false;
	}

	target_addr_t dest = target->ram->start;
	uint32_t size = SPEEDTEST_DEFAULT_SIZE;
	uint32_t count = SPEEDTEST_DEFAULT_COUNT;

	if (argc > 4)
		goto usage;

	char *end = NULL;

	if (argc >= 2) {
		count = strtoul(argv[1], &end, 0);
		if (!end || *end || count == 0U)
			goto usage;
	}

	if (argc >= 3) {
		size = strtoul(argv[2], &end, 0);
		if (!end || *end || size == 0U || size > SPEEDTEST_MAX_SIZE)
			goto usage;
	}

	if (argc >= 4) {
		dest = (target_addr_t)strtoul(argv[3], &end, 0);
		if (!end || *end)
			goto usage;
	}

	bool in_ram = false;
	for (const target_ram_s *ram = target->ram; ram; ram = ram->next) {
		const target_addr_t ram_end = ram->start + (target_addr_t)ram->length;
		if (dest >= ram->start && dest <= ram_end && size <= ram_end - dest) {
			in_ram = true;
			break;
		}
	}
	if (!in_ram) {
		tc_printf(target, "address not in target ram\n");
		return false;
	}

	uint8_t *const buffer = malloc(2 * size);
	if (!buffer) {
		tc_printf(target, "malloc %" PRIu32 " bytes fail\n", size);
		return false;
	}

	for (uint32_t i = 0; i < size; ++i)
		buffer[i] = i & 0xff;

	if (target_mem32_read(target, buffer + size, dest, size)) {
		tc_printf(target, "ram backup fail\n");
		free(buffer);
		return false;
	}

	tc_printf(target, "%" PRIu32 " times writing %" PRIu32 " bytes to 0x%08" PRIx32 "\n", count, size, (uint32_t)dest);

	const rt_tick_t start_tick = rt_tick_get();
	for (uint32_t i = 0; i < count; ++i) {
		if (target_mem32_write(target, dest, buffer, size)) {
			tc_printf(target, "write fail\n");
			free(buffer);
			return false;
		}
	}
	const rt_tick_t end_tick = rt_tick_get();

	if (target_mem32_read(target, buffer, dest, size)) {
		tc_printf(target, "read fail\n");
		free(buffer);
		return false;
	}

	uint32_t i = 0;
	for (i = 0; i < size; ++i) {
		if (buffer[i] != (i & 0xff)) {
			tc_printf(target, "verify fail\n");
			free(buffer);
			return false;
		}
	}

	if (target_mem32_write(target, dest, buffer + size, size)) {
		tc_printf(target, "ram restore fail\n");
		free(buffer);
		return false;
	}

	free(buffer);

	const uint32_t ticks = end_tick - start_tick;
	const uint32_t ms = (ticks * 1000U) / RT_TICK_PER_SECOND;
	const uint64_t bytes = (uint64_t)count * size;

	if (ticks == 0) {
		tc_printf(target, "timing fail\n");
		return false;
	}

	const uint64_t kbytes_per_sec_x100 = (bytes * (uint64_t)RT_TICK_PER_SECOND * 100ULL) / (1024ULL * ticks);
	const uint32_t kbytes_per_sec_int = (uint32_t)(kbytes_per_sec_x100 / 100ULL);
	const uint32_t kbytes_per_sec_frac = (uint32_t)(kbytes_per_sec_x100 % 100ULL);

	tc_printf(target, "%" PRIu64 " bytes in %" PRIu32 " ms = %" PRIu32 ".%02" PRIu32 " kB/s\n", bytes, ms,
		kbytes_per_sec_int, kbytes_per_sec_frac);

	return true;

usage:
	tc_printf(target, "usage: mon speedtest [COUNT [SIZE [ADDR]]]\ndefault: %u, %u, target ram start\n",
		SPEEDTEST_DEFAULT_COUNT, SPEEDTEST_DEFAULT_SIZE);
	return false;
}
