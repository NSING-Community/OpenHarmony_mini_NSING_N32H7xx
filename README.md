# OpenHarmony mini port for NSING N32H7xx (overlay)

This repository holds **only the files added or changed** on top of the official
OpenHarmony source tree. It is an overlay: you keep using the upstream tree, and
apply these files on top of it.

- System type: **mini** (LiteOS-M)
- Target: N32H78x, Cortex-M7 (+ CM4), 128 KiB AXI SRAM
- Baseline: **OpenHarmony 6.1 LTS**, tag `OpenHarmony-v6.1-LTS`
- Toolchain: arm-none-eabi-gcc 10.3.1, gn 2023, ninja 1.12, hb 1.0.0

## Apply

```bash
# 1. base tree (about 25 GB)
repo init -u https://gitcode.com/openharmony/manifest.git \
          -b OpenHarmony-v6.1-LTS --no-repo-verify
repo sync -c
repo forall -c 'git lfs pull'

# 2. this overlay, next to the tree
git clone git@github.com:NSING-Community/OpenHarmony_mini_Nsing_N32H7xx.git ~/ohos-overlay

# 3. apply and build
bash ~/ohos-overlay/tools/apply-overlay.sh ~/ohos
cd ~/ohos
export PATH=$HOME/.local/bin:$PATH
hb set -p n32h7xx_eval
hb build
```

Output: `out/n32h7xx_eval/n32h7xx_eval/OHOS_Image`. Convert and flash:

```bash
arm-none-eabi-objcopy -O ihex   OHOS_Image firmware.hex
arm-none-eabi-objcopy -O binary OHOS_Image firmware.bin
```

## Revert

```bash
bash ~/ohos-overlay/tools/apply-overlay.sh ~/ohos --revert
```

`overlay.manifest` lists every base-tree file this overlay replaces; `--revert`
restores them with `git checkout` and removes the added directories.

## What is inside

| Path | Content |
|---|---|
| `device/soc/Nsing/n32h7xx/` | SoC layer: linker script, startup, SDK shims (I2C/PWM/ADC), vendor firmware |
| `device/board/Nsing/n32h7xx_eval/` | Board layer: build config, `main()`, console, LED, self-contained formatter |
| `vendor/Nsing/n32h7xx_eval/` | Product config: component list, HCS device tree, kernel configs |
| `applications/sample/n32h7xx_eval/` | Demo application: LED, heartbeat, I2C EEPROM, PWM sweep, ADC temperature |
| `drivers/hdf_core/adapter/` | HDF platform drivers for GPIO / UART / I2C / PWM, plus build wiring |
| `kernel/liteos_m/.../los_dispatch.S` | FPU macro fix for Cortex-M7 |
| `docs_Nsing/` | Technical manual, porting SOP, and the `ohos-mcu-porting` skill |

## License

BSD-3-Clause for the code written by NSING, see `LICENSE`.

This repository is an overlay on top of OpenHarmony, so not every file is under
the same terms:

| Part | License |
|---|---|
| Our new code (`device/soc/Nsing`, `device/board/Nsing`, `vendor/Nsing`, `applications/`, `docs_Nsing/`, `tools/`) | BSD-3-Clause |
| Files we modified inside upstream repositories (see `overlay.manifest`) | Apache-2.0, original headers kept |
| Vendor SDK under `device/soc/Nsing/n32h7xx/firmware/`, plus the vendor-derived linker script, startup files and `system_n32h7xx.c` | Nations SDK terms, original headers kept |

`NOTICE` lists the exact file sets. No copyright header was rewritten.

## Notes

- `device/soc/Nsing/n32h7xx/firmware/` is the vendor SDK and keeps its original
  license; everything else is Apache-2.0.
- All text files use LF. Do not let an editor convert them to CRLF: a CRLF tree
  turns every touched file into a whole-file diff.
- XTS: build one image per suite and strip the demos with
  `--gn-args build_xts=true --gn-args xts_suite=<suite> --gn-args app_demo=false`.

## PWM output

The demo drives **PA6 (GTIMA2_CH1, AF11)** at 1 kHz / 50 % fixed, because on this
board PA0 and PA1 belong to Ethernet (PA1 = REF_CLK) and are fragile to drive.

| Macro (`n32_pwm_hw.h`) | Effect |
|---|---|
| `N32_PWM_USE_PA6` = 1 (default) | GTIMA2_CH1 on PA6 / AF11 |
| `N32_PWM_USE_PA6` = 0 | GTIMA1_CH1 on PA0 / AF6 |
| `N32_PWM_USE_CH2` = 0 (default) | PA1 is never muxed or driven (it is ETH_REFCLK) |

Both timer clock bits are enabled (`GTIMx | GTIMxLP`). The LP bit is required:
with `LOSCFG_KERNEL_PM=y` the kernel sleeps when idle and the timer clock is
gated without it, which shows up as "a short burst, then the pin sits flat".
