// SPDX-License-Identifier: GPL-2.0
//
// fp_deepsleep — SAFE PROBE TEMPLATE for NX809J (canoe / 6.12.23-android16-OP-WILD).
//
// STATUS: the original "force the single-FP bit" fix is DISPROVEN (see README).
//   Forcing tcm[+1464]=1 + tcm[+1456]|=1 in syna_dev_enable_lowpwr_gesture corrupts
//   the panel gesture state machine — cfg254 = dt2w | (mask<<13) expects SPECIFIC
//   gesture bits, not a blanket OR — and broke DT2W + AOD-FP without unlocking.
//   The real gap is the AUTH CHAIN (panel already reports aod_areameet_down on the
//   FP press; the framework has no doze-UDFPS sensor to consume it). NOT the touch
//   driver. So this module no longer attempts a fix — it stays a LOG-ONLY probe.
//
// What it still proves (the reusable win): a custom KCFI module loads on this
// kernel and a kprobe fires on real panel suspend with valid struct offsets.
// Use it as a template for future zte_tpd RE. The full loadable-module recipe
// (vermagic/modversions/module_layout-CRC/KCFI/integer-normalize/sig) is in README.
//
// Decompiled zte_tpd offsets (confirmed live by this probe): tcm sub-struct,
//   +1456 = tcm[364] combined gesture mask, +1464 = tcm[366] single-FP bit,
//   +1476 = tcm[369] DT2W (wake_gesture). Live read on full suspend showed
//   fp=0 mask=0x0 dt2w=1 — i.e. DT2W armed, FP scan NOT armed (the gap).

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/ptrace.h>

#define OFF_MASK      1456   /* tcm[364] combined gesture mask  */
#define OFF_SINGLE_FP 1464   /* tcm[366] single-FP gesture bit  */
#define OFF_DT2W      1476   /* tcm[369] wake_gesture (DT2W)     */

static unsigned int hits;

/* Log-only: read (never write) the gesture fields on the suspend arming path.
 * syna_dev_suspend is the entry that fires on FULL panel suspend (deep doze);
 * tethered the device only reaches AOD so it fires transiently — validate
 * untethered + read pstore/dmesg. */
static int pre_suspend(struct kprobe *p, struct pt_regs *regs)
{
	void *tcm = (void *)regs->regs[0];	/* x0 = a1 = tcm */

	if (tcm) {
		u32 fp   = *(u32 *)((char *)tcm + OFF_SINGLE_FP);
		u32 mask = *(u32 *)((char *)tcm + OFF_MASK);
		u32 dt2w = *(u32 *)((char *)tcm + OFF_DT2W);

		pr_info("fp_deepsleep: suspend #%u tcm=%px fp=%u mask=0x%x dt2w=%u\n",
			++hits, tcm, fp, mask, dt2w);
	}
	return 0;
}

static struct kprobe kp = {
	.symbol_name = "syna_dev_suspend",
	.pre_handler = pre_suspend,
};

static int __init fpds_init(void)
{
	int ret = register_kprobe(&kp);

	pr_info("fp_deepsleep: register_kprobe(%s) ret=%d addr=%px\n",
		kp.symbol_name, ret, kp.addr);
	return ret;
}

static void __exit fpds_exit(void)
{
	unregister_kprobe(&kp);
	pr_info("fp_deepsleep: unregistered (hits=%u)\n", hits);
}

module_init(fpds_init);
module_exit(fpds_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("IronShing / NX809J port");
MODULE_DESCRIPTION("Log-only zte_tpd suspend probe (KCFI loadable-module template)");
