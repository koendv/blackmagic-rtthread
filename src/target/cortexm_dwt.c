#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

#include "general.h"
#include "target.h"
#include "target_internal.h"
#include "cortex_internal.h"
#include "cortexm.h"
#include "cortexm_dwt.h"

/* write target DWT_CTRL */
static bool cortexm_dwt_ctrl_write(target_s *const target, const uint32_t new_ctrl)
{
	/*
         * DDI0553B, B14.2. Data Watchpoint and Trace unit:
         * This means that, to initialize POSTCNT, software:
         * 1. Ensures that DWT_CTRL.CYCEVTENA and DWT_CTRL.PCSAMPLENA are set to 0. This can be
         *    achieved with a single write to DWT_CTRL. This is also the reset value of these bits.
         * 2. Writes the required initial value of POSTCNT to the DWT_CTRL.POSTINIT field, leaving
         *    DWT_CTRL.CYCEVTENA and DWT_CTRL.PCSAMPLENA set to 0.
         * 3. Sets either DWT_CTRL.CYCEVTENA or DWT_CTRL.PCSAMPLENA to 1 to enable the POSTCNT
         *    counter.
         * Each of these are separate writes to DWT_CTRL.
         *
         * Writes to DWT_CTRL.POSTINIT are ignored if either DWT_CTRL.CYCEVTENA was set to 1 or
         * DWT_CTRL.PCSAMPLENA was set to 1 prior to the write.
         */

	if (target_mem32_write32(target, CORTEXM_DWT_LAR, CORTEXM_LAR_UNLOCK))
		return false;

	if (new_ctrl & (CORTEXM_DWT_CTRL_PCSAMPLENA | CORTEXM_DWT_CTRL_CYCEVTENA)) {
		const uint32_t arm_mask = CORTEXM_DWT_CTRL_PCSAMPLENA | CORTEXM_DWT_CTRL_CYCEVTENA;
		if (target_mem32_write32(target, CORTEXM_DWT_CTRL, new_ctrl & ~arm_mask))
			return false;
		if (target_mem32_write32(target, CORTEXM_DWT_CTRL, new_ctrl & ~arm_mask))
			return false;
	}
	if (target_mem32_write32(target, CORTEXM_DWT_CTRL, new_ctrl))
		return false;
	return true;
}

static void cortexm_dwt_status(target_s *const target, const uint32_t ctrl, const uint32_t tcr)
{
	tc_printf(target, "DWT_CTRL: 0x%08" PRIx32 "\n", ctrl);
	if (ctrl & CORTEXM_DWT_CTRL_NOTRCPKT)
		tc_printf(target, "PC/data sampling not supported\n");
	if (ctrl & CORTEXM_DWT_CTRL_NOEXTTRIG)
		tc_printf(target, "external trigger not supported\n");
	if (ctrl & CORTEXM_DWT_CTRL_NOCYCCNT)
		tc_printf(target, "cycle counter not supported\n");
	if (ctrl & CORTEXM_DWT_CTRL_NOPRFCNT)
		tc_printf(target, "profiling (event) counters not supported\n");
	tc_printf(target, "PC sampling %s\n", (ctrl & CORTEXM_DWT_CTRL_PCSAMPLENA) ? "on" : "off");
	tc_printf(target, "ITM_TCR: 0x%08" PRIx32 "\n", tcr);
	tc_printf(target, "trace forwarding to SWO: %s\n", (tcr & CORTEXM_ITM_TCR_DWTENA) ? "on" : "off");
}

static void cortexm_dwt_usage(target_s *const target)
{
	tc_printf(target,
		"usage: monitor dwt [enable|disable|status|"
		"clear|0..31|exception|lts <0..3>|gts <0..3>|timestamp]...\n");
}

bool cortexm_dwt(target_s *const target, const int argc, const char **const argv)
{
	if (argc < 2) {
		cortexm_dwt_usage(target);
		return false;
	}

	if (target->target_options & CORTEXM_TOPT_FLAVOUR_V6M) {
		tc_printf(target, "Cortex-M0: DWT not supported.\n");
		return false;
	}

	uint32_t ctrl = 0;
	uint32_t tcr = 0;
	if (target_mem32_read(target, &ctrl, CORTEXM_DWT_CTRL, sizeof(ctrl))) {
		tc_printf(target, "DWT_CTRL read failed\n");
		return false;
	}
	if (target_mem32_read(target, &tcr, CORTEXM_ITM_TCR, sizeof(tcr))) {
		tc_printf(target, "ITM_TCR read failed\n");
		return false;
	}

	bool want_status = false;

	for (int i = 1; i < argc; ++i) {
		const char *const arg = argv[i];
		const size_t arg_length = strlen(arg);
		if (!arg_length)
			continue;
		if (arg[0] >= '0' && arg[0] <= '9') {
			const uint32_t rate = strtoul(arg, NULL, 0);
			if (rate > 31U) {
				tc_printf(target, "rate must be 0..31\n");
				return false;
			}
			/* invert so 0 is slowest, 31 is fastest */
			const uint32_t reg_value = 31U - rate;
			const uint32_t postpreset = reg_value & 0xfU;
			const bool cyctap = (reg_value >> 4U) & 1U;
			ctrl &= ~(CORTEXM_DWT_CTRL_POSTPRESET_MASK | CORTEXM_DWT_CTRL_POSTINIT_MASK | CORTEXM_DWT_CTRL_CYCTAP);
			ctrl |= (postpreset << 1U) | (postpreset << 5U) | (cyctap ? CORTEXM_DWT_CTRL_CYCTAP : 0U);
			ctrl |= CORTEXM_DWT_CTRL_PCSAMPLENA | CORTEXM_DWT_CTRL_CYCCNTENA;
		} else if (arg_length < 2U) {
			tc_printf(target, "'%s' too short\n", arg);
			return false;
		} else if (!strncmp(arg, "enable", arg_length))
			tcr |= CORTEXM_ITM_TCR_DWTENA;
		else if (!strncmp(arg, "disable", arg_length))
			tcr &= ~CORTEXM_ITM_TCR_DWTENA;
		else if (!strncmp(arg, "status", arg_length))
			want_status = true;
		else if (!strncmp(arg, "exception", arg_length))
			ctrl |= CORTEXM_DWT_CTRL_EXCTRCENA;
		else if (!strncmp(arg, "lts", arg_length)) {
			if (i + 1 >= argc || argv[i + 1][0] < '0' || argv[i + 1][0] > '9') {
				tc_printf(target, "usage: ... lts <0..3>\n");
				return false;
			}
			const uint32_t lts = strtoul(argv[++i], NULL, 0);
			if (lts > 3U) {
				tc_printf(target, "lts must be 0..3\n");
				return false;
			}
			tcr &= ~CORTEXM_ITM_TCR_TSPRESCALE_MASK;
			tcr |= lts << CORTEXM_ITM_TCR_TSPRESCALE_SHIFT;
		} else if (!strncmp(arg, "gts", arg_length)) {
			if (i + 1 >= argc || argv[i + 1][0] < '0' || argv[i + 1][0] > '9') {
				tc_printf(target, "usage: ... gts <0..3>\n");
				return false;
			}
			const uint32_t gts = strtoul(argv[++i], NULL, 0);
			if (gts > 3U) {
				tc_printf(target, "gts must be 0..3\n");
				return false;
			}
			tcr &= ~CORTEXM_ITM_TCR_GTSFREQ_MASK;
			tcr |= gts << CORTEXM_ITM_TCR_GTSFREQ_SHIFT;
		} else if (!strncmp(arg, "timestamp", arg_length)) {
			const uint32_t timestamp_mask = CORTEXM_ITM_TCR_ITMENA | CORTEXM_ITM_TCR_TSENA | CORTEXM_ITM_TCR_SYNCENA |
				CORTEXM_ITM_TCR_TRACEBUSID_MASK;
			tcr &= ~timestamp_mask;
			tcr |= CORTEXM_ITM_TCR_ITMENA | CORTEXM_ITM_TCR_TSENA | CORTEXM_ITM_TCR_SYNCENA |
				(0x0dU << CORTEXM_ITM_TCR_TRACEBUSID_SHIFT);
		} else if (!strncmp(arg, "clear", arg_length)) {
			ctrl &= ~(CORTEXM_DWT_CTRL_PCSAMPLENA | CORTEXM_DWT_CTRL_EXCTRCENA | CORTEXM_DWT_CTRL_EVENTS_MASK);
			tcr &= ~(CORTEXM_ITM_TCR_TSENA | CORTEXM_ITM_TCR_SYNCENA);
		} else {
			cortexm_dwt_usage(target);
			return false;
		}
	}

	if (!cortexm_dwt_ctrl_write(target, ctrl)) {
		tc_printf(target, "DWT_CTRL write failed\n");
		return false;
	}
	if (target_mem32_write32(target, CORTEXM_ITM_LAR, CORTEXM_LAR_UNLOCK)) {
		tc_printf(target, "ITM_LAR write failed\n");
		return false;
	}
	if (target_mem32_write32(target, CORTEXM_ITM_TCR, tcr)) {
		tc_printf(target, "ITM_TCR write failed\n");
		return false;
	}

	if (want_status)
		cortexm_dwt_status(target, ctrl, tcr);

	return true;
}
