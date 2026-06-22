# nx809j-fp-deepsleep

Reverse-engineering record + **reusable loadable-KCFI-module recipe** for the ZTE Nubia
RedMagic 11 Pro (NX809J / canoe / SM8850), kernel `6.12.23-android16-OP-WILD` (Coding-BR /
WildKernels build). Started as an attempt at **deep-doze ultrasonic fingerprint unlock**.

## TL;DR status
- ✅ **Reusable win:** custom **KCFI kernel modules now build + load** on this device. The
  full gauntlet is solved (recipe below) — usable for *any* future kernel RE here.
- ❌ **Deep-doze FP via touch driver: DISPROVEN.** Forcing the single-FP gesture bit in the
  touch driver corrupts the panel gesture state machine and breaks DT2W/AOD-FP without
  unlocking. Wrong layer.
- 🎯 **Real direction:** the gap is the **auth chain**, not touch arming — the panel already
  reports the FP-area press (`tpd_ufp_info: aod_areameet_down=true`); the framework has **no
  doze-UDFPS sensor** to consume it. See "Where the fix actually is".

---

## The reusable win — loadable custom KCFI module recipe
Loading a custom `.ko` on this kernel is a **gauntlet**; every gate must be cleared or the
device rejects the module or panics on load. In order:

1. **vermagic / localversion** — kernel relaxes localversion (loads `-4k` modules on `-OP-WILD`),
   so `LOCALVERSION` need not match. Build against **6.12.23** source (GKI base matches).
2. **MODVERSIONS** — kernel is `CONFIG_MODVERSIONS=y`; a non-modversions module → `Exec format
   error`. Build with MODVERSIONS (needs `gendwarfksyms` → host **libdw/elfutils dev headers**,
   `dwarf.h`).
3. **`module_layout` CRC** — depends on `struct module`, which depends on config (MODULE_SIG,
   SCMVERSION, CFI…). Match the device config OR **patch the CRC** post-build to the device's
   value. The device's `module_layout` CRC is **`0xe976b219`** (read from a loaded vendor `.ko`,
   e.g. `fp_goodix.ko` `__versions`). Patch tool: rewrite the `module_layout` entry in the
   module's `__versions` section.
4. **KCFI init tag** — `do_one_initcall` is CFI-checked; a non-CFI module panics with
   `CFI failure at do_one_initcall (expected type: 0x6fbb3035)`. Build with **`CFI_CLANG=y`**
   (6.12 KCFI needs **no LTO** — keep `LTO_NONE=y`).
5. **CFI integer-normalize** — device is `CONFIG_CFI_ICALL_NORMALIZE_INTEGERS=y`
   (`-fsanitize-cfi-icall-experimental-normalize-integers`), which changes the KCFI type-id
   hash. Without it, init type-id = `0x36b1c5a6` ≠ `0x6fbb3035` → still panics. Enable it and
   **re-sync `auto.conf` via `modules_prepare`** (olddefconfig alone doesn't drive the make flag).
6. **MODULE_SIG** — device is `CONFIG_MODULE_SIG=y` (affects `struct module`). Enable it; set
   **`MODULE_SIG_HASH=sha256`** (sha1 GENKEY fails on OpenSSL 3.5 "invalid digest"). SIG_FORCE is
   off so unsigned modules still load (`sig_enforce=N`), but the config must be on for the CRC.

Build needs a **full vmlinux build** for `Module.symvers` (symbol CRCs). Toolchain: AOSP prebuilt
**clang-r547379** (matches `/proc/version`). Beware **parens in the source path** — they break the
kernel make sub-shell; use a paren-free dir.

`.github/workflows/build-ko.yml` encodes this for reproducibility (build against GKI
`android16-6.12` + the flags above + the `module_layout` patch step).

## fp_deepsleep.c
Now a **log-only probe template** (kprobe on `syna_dev_suspend`, reads gesture fields, no
writes) — a safe demonstration of the loadable-module capability + a live diagnostic. Live read
on full suspend: `fp=0 mask=0x0 dt2w=1` (DT2W armed, FP scan not — the gap).

## The mechanism (decompiled zte_tpd, confirmed live)
- Touch driver `zte_tpd` (Synaptics-TCM) owns `/proc/touchscreen` + reports the FP-area press.
- `syna_dev_enable_lowpwr_gesture(tcm, enable, …)` on suspend writes `cfg254 = tcm[369]<DT2W> |
  (tcm[364]<mask> << 13)` and `cfg212 = tcm[366]<single_fp> ? 3 : 0` (3 == FP-region scan ON).
- `tcm[366]` (single-FP, `+1464`) has no sysfs node; nothing arms it in deep doze.
- **Why force-bit failed:** `cfg254` expects specific gesture bits; a blanket `mask|=1` corrupts
  the state machine → DT2W/AOD-FP break. The firmware path is more than "set the bit".

## Where the fix actually is (auth chain, not touch)
Both the kernel-side (this repo) and framework-side investigation converge:
- The **panel already reports** the FP-area press in AOD: `tpd_ufp_info: aod_areameet_down=true`
  (at `TP_POWER_STATUS=1`). So the touch trigger exists.
- The **framework has no doze-UDFPS sensor**: `config_dozeUdfpsLongPressSensorType` is empty,
  `dumpsys sensorservice` lists no FP/long-press sensor, so nothing drives
  `UdfpsController.onAodInterrupt`. The FP auth client is alive screen-off but never triggered.
- ⇒ The real fix is to **bridge `aod_areameet_down` → framework UDFPS** (synthesize a
  doze-UDFPS sensor HAL + set `config_dozeUdfpsLongPressSensorType`, or a daemon that calls
  `onAodInterrupt`). Framework + sensor-HAL work — **not** a kprobe.

## References
Memories: `fp_screen_off_udfps_2026-06-13`, `nx809j-ksu-susfs-kernel`. Decompiled zte_tpd:
`~/RedMagic/dev_reverse_kernel`. Device kernel source: `~/RedMagic/NX809J_kernel_6_12_23`.
