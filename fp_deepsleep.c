// SPDX-License-Identifier: GPL-2.0
//
// nx809j_fp_deepsleep — arm the ultrasonic FOD low-power scan in DEEP doze.
//
// NX809J (canoe / SM8850) ships an ultrasonic under-display FP (Goodix gf95xx).
// The FP-region press is detected & reported by the ZTE Synaptics-TCM touch
// driver (zte_tpd) → panel_event_notifier → zte_fingerprint → ztecmd scan.
//
// In AOD the framework arms single_aod and FP unlock works. In DEEP doze the
// touch driver keeps low-power sensing alive for DT2W (wake_gesture) but the
// "single-FP" gesture bit is never set, so the panel does not scan the FP
// region — no fp_gesture_down, no unlock.
//
// Decompiled (dev_reverse_kernel) zte_tpd, confident offsets (tcm sub-struct):
//   syna_dev_enable_lowpwr_gesture(tcm /*x0*/, enable /*x1*/, ...) writes:
//     cfg254 = tcm[369](DT2W) | (tcm[364]<<13)          ; reported-gesture mask
//     cfg212 = tcm[366] ? 3 : 0                          ; 3 == FP-region scan ON
//   tcm[364] @ +1456 = combined gesture mask
//   tcm[366] @ +1464 = single-FP bit
// Nothing sets tcm[366] in deep doze (no sysfs node exposes single_fp).
//
// FIX: kprobe pre-handler on syna_dev_enable_lowpwr_gesture — when it is called
// to ENABLE (x1 != 0, i.e. the suspend arming path), force the single-FP bit
// (+1464=1) AND OR it into the mask (+1456|=1). The existing body then writes
// cfg212=3 (scan armed) + cfg254 with the FP bit (gesture reported) → the panel
// scans the FP region in deep doze alongside DT2W → fp_gesture_down fires → the
// existing ztecmd auth chain unlocks. No display illumination needed (ultrasonic).
//
// Loads on 6.12.23-android16-OP-WILD: CONFIG_KPROBES=y, KALLSYMS_ALL=y, unsigned
// .ko loading on (sig_enforce=N). Symbol resolved by name via kallsyms.
//
// CAVEAT: arming the FP scan in deep doze has a small power cost (inherent to the
// feature). Validate the hook fires (dmesg) then test untethered (adb USB
// wakelock blocks true deep doze).

#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <asm/ptrace.h>

#define OFF_MASK      1456   /* tcm[364] combined gesture mask  */
#define OFF_SINGLE_FP 1464   /* tcm[366] single-FP gesture bit  */

static bool enable = true;
module_param(enable, bool, 0644);
MODULE_PARM_DESC(enable, "arm the FP-region low-power scan in deep doze (default true)");

static unsigned long hits;

static int handler_pre(struct kprobe *p, struct pt_regs *regs)
{
	void *tcm = (void *)regs->regs[0];          /* x0 = a1 = tcm */
	unsigned int en = (unsigned int)regs->regs[1]; /* x1 = a2 = enable */

	if (enable && tcm && en) {
		*(u32 *)((char *)tcm + OFF_SINGLE_FP) = 1;
		*(u32 *)((char *)tcm + OFF_MASK) |= 1u;
		if (!(hits++ % 16))
			pr_info("nx809j_fp_deepsleep: armed single_fp (tcm=%px en=%u hits=%lu)\n",
				tcm, en, hits);
	}
	return 0;
}

static struct kprobe kp = {
	.symbol_name = "syna_dev_enable_lowpwr_gesture",
	.pre_handler = handler_pre,
};

static int __init fpds_init(void)
{
	int ret = register_kprobe(&kp);

	pr_info("nx809j_fp_deepsleep: register_kprobe(%s) ret=%d addr=%px enable=%d\n",
		kp.symbol_name, ret, kp.addr, enable);
	return ret;
}

static void __exit fpds_exit(void)
{
	unregister_kprobe(&kp);
	pr_info("nx809j_fp_deepsleep: unregistered (hits=%lu)\n", hits);
}

module_init(fpds_init);
module_exit(fpds_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IronShing / NX809J port");
MODULE_DESCRIPTION("Arm ultrasonic FOD low-power scan in deep doze (zte_tpd kprobe)");
