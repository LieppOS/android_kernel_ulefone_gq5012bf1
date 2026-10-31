# MT6369 ACCDET TRRS AUX and legacy h2w validation

Validation date: 2026-10-31. Hardware-verified on the physical
Ulefone Armor 29 Pro (GQ5012BF1), MT6878T, slot `_b`, Android 17
userdebug/test-keys, verified boot green.

## Source identity

| Item | Value |
| --- | --- |
| Branch | `integration/aux-detection` |
| Base | `a79b9370d` |
| Fallback commit | `e9eadab7e` |
| `h2w` switch commit | `02d1effb7` |
| `h2w` uevent fix | `b91b87ad2` |
| Kernel release | `6.1.115-mt6878-gq5012bf1-c29+` |
| Toolchain | `artifacts/toolchains/clang-r487747c` |

## Problem

A 4-pole TRRS accessory with an open microphone contact leaves the MT6369
ACCDET FSM at `AB_11` and never raises `ACCDET_IRQ`. The EINT0 plug-in is
therefore never reported, so Android keeps media on the speaker even though
the jack is physically inserted.

Separately, this userspace image has
`config_useDevInputEventForAudioJack=false`. Even when the driver reports the
jack through the input device, `WiredAccessoryManager` ignores the `SW_*`
events. A runtime RRO targeting `android` could make `cmd overlay lookup`
return `true`, but `InputManager` still initialized with `false` because the
overlay is not applied early enough for `system_server`.

## Implementation

- `e9eadab7e` arms a one-second delayed work after `enable_accdet(0)` on
  EINT0 plug-in. If EINT0 still reports a plug, `MEM_IN` remains `AB_11`,
  and no other path classified the accessory, it reports
  `HEADSET_NO_MIC`. Normal ACCDET classification wins when it completes.
- `02d1effb7` exposes a legacy `/sys/class/switch/h2w` device with a `state`
  attribute. States: `1` headset with mic, `2` headphones/no mic, `32`
  lineout, `0` unplug. This is the path `WiredAccessoryManager` uses when
  `config_useDevInputEventForAudioJack` is false.
- `b91b87ad2` fixes a `system_server` crash in the first `h2w`
  implementation. A bare `KOBJ_CHANGE` uevent does not contain
  `SWITCH_NAME`/`SWITCH_STATE`; `WiredAccessoryManager` dereferences the
  missing name and throws a `NullPointerException`. Repeated crashes
  triggered a RescueParty reboot. The fix uses `kobject_uevent_env()` with
  the legacy switch-class variables.

The runtime RRO `com.lieppos.audiojackoverlay` was abandoned, disabled, and
uninstalled. The kernel `h2w` switch is the active integration path.

## Build verification

- Module builds cleanly against the C58 kernel ABI.
- All imported symbol CRCs match `vmlinux.symvers`, including
  `device_create`, `device_create_file`, `device_destroy`,
  `device_remove_file`, `_dev_warn`, `kobject_uevent_env`,
  `sysfs_notify`, and `__class_create`.
- `checkpatch.pl`: 0 errors, 0 warnings.

| Artifact | SHA256 |
| --- | --- |
| `mt6369-accdet.ko` | `f83885116ab7fea756e0f1e98de22addea16b35b33b08a2258dd6119c40dd90b` |
| `vendor_dlkm_b` image | `4a3e89f0ba3cd65da61621abb8e144d2457834274971167f4dd3153799bdd195` |
| `vbmeta_b` image | `96e6f498f5908ece9c83d73734883d5132e773f986f278fd725d52491557ada6` |

On-device hashes of `vbmeta_b`, `vendor_dlkm_b`, and
`/vendor_dlkm/lib/modules/mt6369-accdet.ko` matched the flashed images.

## Hardware results

| Test | Result |
| --- | --- |
| 4-pole TRRS with open mic contact | `HEADPHONE(3-pole) PlugIn` plus `AUX(floating mic) PlugIn, report HEADSET_NO_MIC`; `h2w=2`; Android headphone; audio routed to jack. |
| 3-pole TRS headphones | `HEADPHONE(3-pole) PlugIn`; no fallback line; `h2w=2`; Android headphone. |
| Sony PS4 TRRS headset, mic switch muted | Same as the open-mic case: fallback to headphones, `h2w=2`. The muted mic is an open contact. |
| Sony PS4 TRRS headset, mic switch on | `MICROPHONE(4-pole) PlugIn`; `0x26b6=0x55`; `ACCDET_IRQ` increments; `h2w=1`; Android `type:0x4 (headset)` plus `0x80000010` (wired headset input); audio and mic routed. |
| Plain AUX cable to music center | Native `HEADPHONE(3-pole) PlugIn`; no fallback; `h2w=2`; audio routed. |
| Unplug from `h2w=1` | `MICROPHONE(4-pole) PlugOut`; `h2w=0`; AudioService disconnected; `system_server` PID unchanged; no reboot. |
| Unplug from `h2w=2` | `HEADPHONE(3-pole) PlugOut`; `h2w=0`; AudioService disconnected; `system_server` PID unchanged; no reboot. |
| Re-plug after either state | `h2w` returns to `1` or `2`; audio reconnects; no crash. |
| Boot with cable inserted | `h2w` reports the seated accessory at boot; Android lists it. |
| Repeated insert/remove (20x) | Not run. |
| Suspend/resume with cable seated | Not run. |
| Dummy plug / empty jack | Not run. |

## Known limitations

- `AB_11` is electrically identical to an open/floating mic contact. A muted
  headset or a true floating-mic TRRS cable falls back to headphones (audio
  output works, no mic). A headset with a working, unmuted mic classifies
  normally as `MICROPHONE(4-pole)`.
- The `h2w` switch is a compatibility shim for this userspace image. It does
  not replace the input-device `SW_*` reports, which continue to work in
  parallel.
- The validation used one PS4 TRRS headset, one 3-pole TRS cable, and one
  plain AUX cable. Other accessories and the unrun stress tests above remain
  unverified.
