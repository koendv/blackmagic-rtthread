#include <ctype.h>
#include <inttypes.h>

#include "general.h"
#include "gdb_packet.h"
#include "target.h"
#include "memwatch.h"
#if CONFIG_BMDA == 1
#include <unistd.h>
#endif

memwatch_s memwatch_table[MEMWATCH_NUM];
uint32_t memwatch_cnt = 0;
bool memwatch_timestamp = false;

void poll_memwatch(target_s *cur_target)
{
	union val32_u {
		uint32_t i;
		volatile float f;
	} val;

	char buf[64];
	char timestamp[64];
	uint32_t len;
	if (!cur_target || (memwatch_cnt == 0))
		return;

	for (uint32_t i = 0; i < memwatch_cnt; i++) {
		if (!target_mem32_read(cur_target, &val.i, memwatch_table[i].addr, sizeof(val.i)) &&
			(val.i != memwatch_table[i].value)) {
			if (memwatch_timestamp)
				snprintf(timestamp, sizeof(timestamp), "%" PRIu32 " ", platform_time_ms());
			else
				timestamp[0] = '\0';
			switch (memwatch_table[i].format) {
			case MEMWATCH_FMT_SIGNED:
				len = snprintf(buf, sizeof(buf), "%s%s %" PRId32 "\r\n", timestamp, memwatch_table[i].name, val.i);
				break;
			case MEMWATCH_FMT_UNSIGNED:
				len = snprintf(buf, sizeof(buf), "%s%s %" PRIu32 "\r\n", timestamp, memwatch_table[i].name, val.i);
				break;
			case MEMWATCH_FMT_FLOAT:
				len = snprintf(buf, sizeof(buf), "%s%s %.*g\r\n", timestamp, memwatch_table[i].name,
					memwatch_table[i].precision, val.f);
				break;
			case MEMWATCH_FMT_HEX:
			default:
				len = snprintf(buf, sizeof(buf), "%s%s 0x%" PRIx32 "\r\n", timestamp, memwatch_table[i].name, val.i);
				break;
			}
			buf[sizeof(buf) - 1] = '\0';
			debug_serial_send_stdout(buf, len);
			memwatch_table[i].value = val.i;
		}
	}
	return;
}

/* `mon memwatch` command handler */
bool cortexm_memwatch(target_s *target, int argc, const char **argv)
{
	memwatch_format_e fmt = MEMWATCH_FMT_HEX;
	char name[MEMWATCH_STRLEN] = {0};
	int precision = 0;
	char ch;

	if (argc == 2 && strncmp(argv[1], "status", strlen(argv[1])) == 0) {
		union val32_u {
			uint32_t i;
			volatile float f;
		} val;

		gdb_outf("address    variable    fmt value\r\n");
		for (int32_t i = 0; i < memwatch_cnt; i++) {
			val.i = memwatch_table[i].value;
			gdb_outf("0x%08" PRIx32 " %-12s ", memwatch_table[i].addr, memwatch_table[i].name);
			switch (memwatch_table[i].format) {
			case MEMWATCH_FMT_SIGNED:
				gdb_outf("s  %" PRId32 "\r\n", val.i);
				break;
			case MEMWATCH_FMT_UNSIGNED:
				gdb_outf("u  %" PRIu32 "\r\n", val.i);
				break;
			case MEMWATCH_FMT_FLOAT:
				gdb_outf("f%d %.*g\r\n", memwatch_table[i].precision, memwatch_table[i].precision, val.f);
				break;
			case MEMWATCH_FMT_HEX:
				gdb_outf("x  %" PRIx32 "\r\n", val.i);
				break;
			default:
				gdb_outf("?\r\n");
			}
		}
		return true;
	}

	memset(memwatch_table, 0, sizeof(memwatch_table));
	memwatch_cnt = 0;
	memwatch_timestamp = false;
	/* target has to support debugger memory access while running */
	if (target_mem_access_needs_halt(target)) {
		gdb_out("memwatch not supported for target\n");
		return true;
	}
	for (int32_t i = 1; i < argc; i++) {
		if (argv[i][0] == '/') {
			/* format follows */
			switch (argv[i][1]) {
			case 'd':
				fmt = MEMWATCH_FMT_SIGNED;
				break;
			case 'u':
				fmt = MEMWATCH_FMT_UNSIGNED;
				break;
			case 'f':
				fmt = MEMWATCH_FMT_FLOAT;
				precision = 6;
				ch = argv[i][2];
				if ((ch >= '0') && (ch <= '9'))
					precision = ch - '0';
				break;
			case 'x':
				fmt = MEMWATCH_FMT_HEX;
				break;
			case 't':
				memwatch_timestamp = true;
				break;
			default:
				break;
			}
		} else if (isalpha(argv[i][0])) {
			/* name  follows */
			strncpy(name, argv[i], sizeof(name));
			name[sizeof(name) - 1] = '\0';
		} else if (isdigit(argv[i][0])) {
			/* address follows */
			uint32_t addr = strtoul(argv[i], NULL, 0);

			/* add new name, format and address to memwatch table */
			if (name[0] == '\0') {
				/* no name given, use address as name */
				snprintf(name, MEMWATCH_STRLEN, "0x%08" PRIx32, addr);
				name[MEMWATCH_STRLEN - 1] = '\0';
			}
			memwatch_table[memwatch_cnt].addr = addr;
			memwatch_table[memwatch_cnt].value = 0;
			memwatch_table[memwatch_cnt].format = fmt;
			memwatch_table[memwatch_cnt].precision = precision;
			memcpy(memwatch_table[memwatch_cnt].name, name, MEMWATCH_STRLEN);
			memwatch_cnt++;
			memset(name, 0, sizeof(name));
			gdb_outf("0x%08" PRIx32 " ", addr);
			if (memwatch_cnt == MEMWATCH_NUM)
				break;
		} else {
			gdb_outf("?");
			break;
		}
	}
	gdb_outf("\n");
	return true;
}
