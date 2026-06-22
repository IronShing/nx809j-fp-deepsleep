# nx809j-fp-deepsleep

A kprobe kernel module that makes **ultrasonic fingerprint unlock work from DEEP doze**
(phone fully dark, AOD's deepest state) on the ZTE Nubia RedMagic 11 Pro
(NX809J / canoe / SM8850), LineageOS 23.2.

## The problem
- The FP sensor is **ultrasonic** (Goodix `gf95xx`) — no display illumination needed.
- AOD-state FP unlock already works (framework arms `single_aod`).
- In **deep doze** the ZTE Synaptics-TCM touch driver (`zte_tpd`) keeps low-power
  sensing alive for double-tap-to-wake, but never sets the **single-FP** gesture bit,
  so the panel doesn't scan the FP region → no `fp_gesture_down` → no unlock.
- The single-FP bit has **no sysfs node** (only `single_aod`/`single_game`/`single_tap`/
  `wake_gesture` are exposed), so it cannot be armed from userspace.

## The fix (decompiled, confident offsets)
`syna_dev_enable_lowpwr_gesture(tcm /*x0*/, enable /*x1*/, ...)` writes the panel
dynamic config on suspend:
- `cfg254 = tcm[369](DT2W) | (tcm[364] << 13)` — the reported-gesture mask
- `cfg212 = tcm[366] ? 3 : 0` — **3 == FP-region scan ON**

`tcm[366]` (`+1464`, single-FP) is never set in deep doze. This module registers a
**kprobe pre-handler** on that function; when called to enable (x1 != 0, the suspend
path) it forces:
- `*(u32*)(tcm + 1464) = 1`   → `cfg212 = 3` (FP scan armed)
- `*(u32*)(tcm + 1456) |= 1`  → `cfg254` carries the FP bit (gesture reported)

→ the panel scans the FP region in deep doze alongside DT2W → `fp_gesture_down` fires
→ the existing `zte_fingerprint`/`ztecmd` ultrasonic auth chain unlocks the device.

## Build (reproducible)
GitHub Actions: **Actions → Build fp_deepsleep.ko → Run workflow** → download the
`fp_deepsleep-ko` artifact. Builds against Android GKI `android16-6.12` common (same
GKI base as the device kernel `6.12.23-android16-OP-WILD`). The module references only
KMI-stable core symbols (`register_kprobe`/`printk`), so it loads on the device kernel,
which also tolerates vermagic mismatch (proven by the stock vendor `.ko`s).

Local: `make KDIR=/path/to/prepared/6.12.x/kernel`.

## Install / test
Requirements on device: `CONFIG_KPROBES=y`, `sig_enforce=N` (unsigned `.ko` loading) —
both true on the OP-WILD kernel.

```sh
adb push fp_deepsleep.ko /data/local/tmp/
adb shell su -c 'insmod /data/local/tmp/fp_deepsleep.ko'
adb shell 'dmesg | grep nx809j_fp_deepsleep'   # expect register_kprobe ret=0
```
Then **untethered** (adb USB wakelock blocks true deep doze): let the phone go fully
dark, press the FP icon → should unlock. `enable=0` module param disables arming.

Persistent: ship as a KernelSU module (`post-fs-data.sh` → `insmod`), mirroring the
enforcing module.

## Caveat
Arming the FP-region scan in deep doze costs a little power — inherent to "press-to-
unlock from fully off". The hook-fires/auth-chain still needs live untethered
confirmation. Offsets are from `dev_reverse_kernel` decompilation; re-verify if the
`zte_tpd` build changes.
