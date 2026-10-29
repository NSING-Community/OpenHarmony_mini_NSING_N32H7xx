# 05 校验手册

原则：**"编译通过"不是证据**。每条结论都要能在产物里找到对应痕迹。

约定：`$OUT` = 构建输出目录，`$ELF` = `$OUT/OHOS_Image`，工具前缀按目标架构替换
（ARM 用 `arm-none-eabi-`，RISC-V 用 `riscv*-unknown-elf-`）。

## 一、启动链

```bash
# 1. 内核堆是否等于链接脚本设定值
$PFX-nm $ELF | grep -E '__heap_start|__heap_end'
# 期望：两者之差 == _Min_Heap_Size（堆是 NOLOAD，改它不改变文件大小）

# 2. 是否有浮点上下文批量存取（C/ASM 一致的证据）
$PFX-objdump -d $ELF | grep -E 'vstmdb|vldmia'
# 期望：出现 {d8-d15} 之类的批量存取

# 3. 向量表对齐（看符号地址）
$PFX-nm $ELF | grep -iE 'g_hwiForm|vector'
# 期望：地址按配置值（如 0x800）对齐

# 4. 版本串（每次编译唯一）
$PFX-strings $ELF | grep 'BUILD='
```

## 二、HDF / 驱动

```bash
# 5. 驱动表字节数：每个驱动 4 字节（32 位目标）
$PFX-nm $ELF | grep _hdf_drivers
# 期望：end - start = 驱动数 × 4；新增驱动只会 +4

# 6. 驱动符号是否真的进镜像
$PFX-nm $ELF | grep -i '<你的驱动入口名>'

# 7. 参数是否读到了（运行时）
#    日志里不应出现"使用默认值 / no hcs node"一类提示；
#    若驱动有默认值分支，把读到的值打印出来核对。

# 8. hcs 两段式一致性
python3 scripts/check_hcs.py <vendor>/hdf_config
# 检查：每个 deviceMatchAttr 都能找到同名 match_attr；未 include 的参数文件会被报出

# 9. 驱动没被 gc 掉（链接脚本）
grep -n 'KEEP(\*(.hdf.driver))' <soc>/liteos_m/board.ld
```

## 三、demo

```bash
# 10. 应用入口是否进了镜像
$PFX-nm $ELF | grep -iE 'App.*Entry'
# 产品固件：应列出入口名（strip 过的镜像可能只保留一部分，作参考而非精确计数）
# XTS 固件（app_demo=false）：应为空，且启动日志 MODULE_INIT(run) : 0 entry

# 11. 日志串
$PFX-strings $ELF | grep -E '\[PWM\]|\[ADC\]|heartbeat'
```

运行时判据：`MODULE_INIT(run) : N entry` 的 N 与源码里 `SYS_RUN` 个数一致；
XTS 固件应为 0。

## 四、XTS

```bash
# 12. 套件是否被链接进来
$PFX-strings $ELF | grep -i '<套件名>'

# 13. 二进制体积对比（正常固件 vs XTS 固件）
ls -l $OUT/OHOS_Image
```

运行时按四行判读，见 `03-xts.md`。

## 五、产物与提交

```bash
# 14. 生成 hex / bin
$PFX-objcopy -O ihex   $ELF <name>.hex
$PFX-objcopy -O binary $ELF <name>.bin

# 15. 全量重编后核对目标文件数（判"是否真的重编"）
find $OUT/obj -name '*.o' | wc -l

# 16. 提交前扫换行与"整文件全改"
bash scripts/check_crlf.sh <repo> [<repo>...]
```

## 六、脚本用法

| 脚本 | 用法 | 检查内容 |
|---|---|---|
| `verify_image.sh` | `verify_image.sh <ELF> <期望驱动数>` | 驱动表、堆差值、浮点指令、版本串、demo 入口数 |
| `check_hcs.py` | `check_hcs.py <hdf_config 目录>` | 两段式三处关系 |
| `check_crlf.sh` | `check_crlf.sh <repo>...` | 整文件全改（疑似换行污染）与 CRLF 残留 |
| `new_driver.sh` | `new_driver.sh <外设> <厂商前缀> <输出目录>` | 生成四文件骨架 |

脚本失败即视为闸门不过，不要用"看起来正常"绕过。
