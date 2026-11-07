# stm_ble_ota

[English](README.md) | 简体中文

一个可直接构建的 STM32C562RE 小型参考工程，包含：

- 周期传感器采集（I2C 上的 SHT30 + 一路 ADC 光敏电压）；
- 通过运行 ESP-AT 的 ESP32 完成 WiFi 配网；
- 由同一颗 ESP32 承载的 BLE GATT 服务；
- 带 bootloader、双应用槽和持久化升级状态的安全 OTA。

代码按 `bsp` / `drivers` / `services` / `app` 分层，外设驱动直接操作寄存器而
不使用 HAL，需要移植的外设面很小。

**状态：** 固件与主机侧测试已在本地构建通过。所有代码均未在真实硬件上运行，
未验证项在 [KNOWN_ISSUES.md](KNOWN_ISSUES.md) 中逐条列出，请不要把它当作已经
验证过的产品。

## 系统结构

```
        ┌──────────────────────────┐
        │  ESP32（ESP-AT 固件）    │
        │  WiFi STA + BLE GATT srv │
        └───────▲──────────▲───────┘
      USART1    │ AT / URC │
                │          │
        ┌───────┴──────────┴───────┐        ┌───────────────┐
        │      STM32C562RE         │  I2C1  │  SHT30 (0x44) │
        │ bootloader + 应用程序    │◄──────►│               │
        │                          │  ADC1  │  光敏分压     │
        └───────▲──────────────────┘        └───────────────┘
                │ USART2（JSON + OTA 帧）
        ┌───────┴──────────┐
        │ PC / ST-LINK VCP │  tools/ota_host.py
        └──────────────────┘
```

USART2 链路的用途是：在没有 BLE 中心设备或没有自定义 ESP-AT 固件时，也能
完整走通配网与 OTA 流程。

## 硬件选型

| 位号 | 器件 | 说明 |
| --- | --- | --- |
| 主控 | STM32C562RET6，Cortex-M33 + FPU，最高 144 MHz，512 KB 双 bank Flash，128 KB SRAM | NUCLEO-C562RE 开发板 |
| 协处理器 | 运行 ESP-AT 的 ESP32 模组 | WiFi STA + BLE GATT 服务器，串口连接 |
| 传感器 | SHT30 | I2C 地址 `0x44`，100 kHz |
| 光敏输入 | 光敏电阻 / 分压 | ADC1_IN0，0..3.3 V |
| 调试烧录 | Nucleo 板载 ST-LINK | SWD + 虚拟串口 |

固件运行在复位默认时钟：HSI 144 MHz 三分频 = **48 MHz**。未实现 144 MHz PLL
配置。

### 接线

| 信号 | STM32 引脚 | 方向 | 对端 |
| --- | --- | --- | --- |
| ESP TX / RX | PA9（USART1_TX，AF7） | 输出 | ESP RX |
| ESP RX / TX | PA10（USART1_RX，AF7） | 输入 | ESP TX |
| 上位机 TX | PA2（USART2_TX，AF7） | 输出 | ST-LINK VCP RX |
| 上位机 RX | PA3（USART2_RX，AF7） | 输入 | ST-LINK VCP TX |
| I2C1 SCL | PB6（AF4） | 输出 | SHT30 SCL，4.7 kΩ 上拉到 3.3 V |
| I2C1 SDA | PB7（AF4） | 双向 | SHT30 SDA，4.7 kΩ 上拉到 3.3 V |
| SHT30 VDD/GND | 3.3 V / GND | — | 100 nF 去耦 |
| 光敏输入 | PA0（ADC1_IN0） | 输入 | 分压输出，≤ 3.3 V |
| LED | PA5 | 输出 | Nucleo LD2 |
| 按键 | PC13 | 输入 | Nucleo B1（已初始化，未使用） |

ESP32 与 STM32 必须共地。ESP32 为 3.3 V 逻辑，禁止把 5 V 接入 STM32 引脚。
USART2 为 115200 8N1；Nucleo 上需要确认 ST-LINK VCP 的焊桥确实连到
PA2/PA3（见 KNOWN_ISSUES）。

## Flash 分区

器件：512 KB 程序 Flash，两个 256 KB bank，擦除页 8 KB。

| 区域 | 地址 | 大小 | 页号 | 内容 |
| --- | --- | --- | --- | --- |
| Bootloader | `0x0800_0000` | 32 KB | 0–3 | bootloader，拷贝循环放在 RAM |
| Slot A（应用） | `0x0800_8000` | 128 KB | 4–19 | 正在运行的 APP，链接地址 |
| Bank1 预留 | `0x0802_8000` | 96 KB | 20–31 | 未使用 |
| Slot B（OTA 暂存） | `0x0804_0000` | 128 KB | 32–47 | 下载的新镜像 |
| OTA 元数据 | `0x0806_0000` | 16 KB | 48–49 | 双页交替记录 |
| WiFi 配置 | `0x0806_4000` | 16 KB | 50–51 | 双页交替记录 |
| Bank2 预留 | `0x0806_8000` | 96 KB | 52–63 | 未使用 |

启动流程：

1. bootloader 读取 OTA 记录。
2. 状态为 `READY_TO_BOOT` 或 `INSTALLING` 时，先校验 slot B（大小、向量表、
   CRC），再擦除 slot A 并把 B 拷贝到 A。中断的拷贝会在下次复位时重试
   （拷贝开始前 slot B 完好，擦除只影响 slot A）。
3. slot A 校验通过则跳转；否则停在 bootloader（LED 慢闪循环）。
4. `INSTALLED` 时快闪 3 次后启动；`INSTALLING`/`READY_TO_BOOT` 快闪 2 次；
   停在 bootloader 时慢闪。

擦写函数与拷贝循环位于 `.ramfunc`，因为 bootloader 擦除的是自己所在的
bank。APP 写的是另一个 bank 的 slot B，双 bank 读写并行，因此不需要 RAM
副本。

## BLE GATT 服务

ESP32 以 GATT 服务器方式运行 ESP-AT。固件假定服务索引为 1、特征索引 0–4，
详见 [docs/protocol.md](docs/protocol.md) 与
[docs/esp32_at_custom_service.md](docs/esp32_at_custom_service.md)。

| 特征 | UUID（16 位） | 属性 | 内容 |
| --- | --- | --- | --- |
| — | `0xFF00` 服务 | — | 自定义 profile |
| 0 | `0xFF01` info | 读、通知 | JSON 设备信息 |
| 1 | `0xFF02` sensor | 通知 | 20 字节传感器数据 |
| 2 | `0xFF03` 配网 | 写、通知 | `wifi.set` / `wifi.clear` / `wifi.status` |
| 3 | `0xFF04` OTA 控制 | 写、通知 | BEGIN / END / ABORT、进度、结果 |
| 4 | `0xFF05` OTA 数据 | 写 | 512 字节镜像分片 |

默认 MTU 下每次通知最多 20 字节，接收端需要跨通知重组帧。

## AT 指令交互

```
AT                 → OK          （最多 5 次重试）
ATE0               → OK
AT+CWMODE=1        → OK
AT+CWAUTOCONN=1    → OK
AT+CWJAP="ssid","pass"
                   ← WIFI CONNECTED
                   ← WIFI GOT IP
                   → OK                 （20 s 超时）
```

BLE 初始化：

```
AT+BLEINIT=2
AT+BLENAME="STM-BLE-OTA"
AT+BLEGATTSSRVCRE
AT+BLEGATTSSRVSTART
AT+BLEADVPARAM=50,150,0,0,7,0,0,0
AT+BLEADVDATA="020106030300FF"
AT+BLEADVSTART
```

配网成功后凭据写入配置记录区；在未收到 `WIFI GOT IP` 期间，每 30 s 重连一次。
`wifi.clear` 执行 `AT+CWQAP` 并擦除两个配置页。

完整的时序、URC 格式与错误处理见 [docs/protocol.md](docs/protocol.md)。

## OTA 数据包格式

与传输方式无关，16 字节头 + 载荷（≤ 512 字节）：

```
0  u8  sync0  = 0xA5
1  u8  sync1  = 0x5A
2  u8  type       0x10 BEGIN, 0x11 DATA, 0x12 END, 0x13 ABORT,
                  0x14 PROGRESS, 0x15 RESULT
3  u8  flags      必须为 0
4  u16 seq
6  u16 payload_len
8  u32 offset     镜像内字节偏移，仅 DATA 使用
12 u32 payload_crc32   载荷的 CRC-32/IEEE
16 N  payload
```

BEGIN 载荷：`image_size`、`image_crc32`、`version`（`0x00MMmmpp`）、
`build_id`、`hardware_id`（必须等于 `0x0050`）、保留字段。

状态：`IDLE → RECEIVING → VERIFYING → READY_TO_BOOT`；大小/CRC/顺序/超时/
中止等错误会进入 `ERROR`。设备会校验每一帧 CRC、整包 CRC，以及从 Flash
回读的实际字节 CRC。版本低于当前运行版本会被拒绝。

失败处理：

- slot B 校验通过前不擦除 slot A；
- 拷贝中断后下次复位自动重试；
- 连续 3 次失败后状态置为 `ERROR`，旧 slot A 仍然可以启动；
- 校验完成后才写元数据，元数据用双页交替记录，写入过程中掉电不会丢失上一份。

完整状态机、原因码与持久化格式见 [docs/protocol.md](docs/protocol.md)。

## 目录结构

```
firmware/
  common/      协议、CRC、记录存储、元数据、安装决策、滤波器
  bootloader/  校验、拷贝、跳转、Flash 驱动、main
  app/
    bsp/       时钟节拍、GPIO、USART、I2C、ADC、Flash（寄存器级）
    drivers/   SHT30、ESP-AT 客户端
    services/  传感器、WiFi、BLE、OTA
    app/       上位机链路解复用、main、JSON 路由
    ld/        app.ld
  vendor/      CMSIS core、STM32C5xx 设备头、启动文件、system
cmake/         arm-none-eabi 工具链文件
tests/         主机侧单元测试 + 假 Flash
tools/         固件打包与配网/OTA 上位机脚本
docs/          协议、ESP-AT 自定义服务、移植说明
```

## 编译

需要 CMake ≥ 3.22、Ninja，以及**官方 Arm GNU Toolchain**（自带 newlib）。
Homebrew 中不带 newlib 的 `arm-none-eabi-gcc` 无法链接本工程。

工具链查找顺序：

1. 环境变量 `ARM_TOOLCHAIN_BIN`；
2. `$HOME/tools/arm-gnu-toolchain-*/bin`；
3. `/Applications/ArmGNUToolchain/*/bin`、`/Applications/ARM/bin`；
4. `PATH` 中的 `arm-none-eabi-gcc`（会给出警告，可能在链接阶段失败）。

编译固件：

```bash
cmake --preset arm
cmake --build --preset arm
```

产物：

```
build/arm/stm_ble_ota_bootloader.bin / .hex
build/arm/stm_ble_ota_app.bin / .hex
```

本仓库实测尺寸：bootloader 占用 3,976 B Flash / 5 KB RAM，APP 占用
18,048 B Flash / 9,432 B RAM，均在各自分区内。

编译并运行主机测试：

```bash
cmake -S tests -B build-host
cmake --build build-host
./build-host/unit_tests
```

## 烧录与调试（纯命令行）

推荐使用 `STM32CubeProgrammer` CLI：

```bash
STM32_Programmer_CLI -c port=SWD -w build/arm/stm_ble_ota_bootloader.bin 0x08000000 -v
STM32_Programmer_CLI -c port=SWD -w build/arm/stm_ble_ota_app.bin       0x08008000 -v
```

芯片非空时先整片擦除：

```bash
STM32_Programmer_CLI -c port=SWD -e all
```

也可以使用 OpenOCD，但并非所有 OpenOCD 版本都带 STM32C5 Flash 驱动，使用前
需确认（见 KNOWN_ISSUES）。

**不需要安装任何 VS Code STM32 插件。** 编译、烧录、OTA 全部走命令行；
可选的 Cortex-Debug 扩展只用于源码级调试，本仓库的任何流程都不依赖它。

## 上位机工具

```bash
python3 -m pip install -r tools/requirements.txt
```

打包镜像（把版本、硬件 ID 与 CRC 写入 `.ota` 容器）：

```bash
python3 tools/firmware_tool.py inspect build/arm/stm_ble_ota_app.bin
python3 tools/firmware_tool.py pack build/arm/stm_ble_ota_app.bin \
    --version 0.1.0 --hw 0x0050 -o build/app-0.1.0.ota
```

通过 PC 链路配网：

```bash
python3 tools/ota_host.py --serial /dev/cu.usbmodemXXXX info
python3 tools/ota_host.py --serial /dev/cu.usbmodemXXXX wifi set \
    --ssid MyNetwork --password secret
python3 tools/ota_host.py --serial /dev/cu.usbmodemXXXX wifi status
python3 tools/ota_host.py --serial /dev/cu.usbmodemXXXX wifi clear
```

OTA 全流程（含复位后回读版本）：

```bash
python3 tools/ota_host.py --serial /dev/cu.usbmodemXXXX ota build/app-0.1.0.ota --verify
```

BLE 方式（需要 `bleak` 与自定义 ESP-AT 服务）：

```bash
python3 tools/ota_host.py --ble --name STM-BLE-OTA info
python3 tools/ota_host.py --ble --name STM-BLE-OTA ota build/app-0.1.0.ota --verify
```

首次联调建议先用串口：它使用完全相同的 JSON 命令和二进制 OTA 帧，且不依赖
协处理器。

## 移植

`firmware/common`、协议逻辑与滤波算法都是可移植的 C 代码。更换主控需要修改
BSP、Flash 驱动、链接脚本、启动文件和 `flash_layout.h`；
[docs/porting.md](docs/porting.md) 列出了每个需要修改的文件，以及单 bank
器件必须把擦除代码放进 RAM 等 Flash 几何约束。

## 已知限制

未在真实硬件上验证：启动、跳转、Flash 擦写、传感器、WiFi 连接、BLE
profile，以及两种传输方式下的 OTA。时钟仅支持 48 MHz；I2C 时序、ADC 精度
和 Flash 时序均为理论推导，未实测。ESP-AT 需要重新构建自定义服务表，官方
固件不包含 `0xFF00`。OTA 仅提供 CRC 完整性校验，没有签名与加密。WiFi
密码以明文保存在片内 Flash。

完整清单见 [KNOWN_ISSUES.md](KNOWN_ISSUES.md)。

## 型号事实与出处

开发期间已按 ST 官方资料核对：

- STM32C562RE：Cortex-M33 + FPU，144 MHz，512 KB 双 bank Flash，128 KB SRAM
  （[数据手册](https://www.st.com/resource/en/datasheet/stm32c562ce.pdf)、
  [产品页](https://www.st.com/en/microcontrollers-microprocessors/stm32c562re.html)）。
- RM0522《STM32C5 系列参考手册》V1.0，2026-02-27 —— Flash 架构与寄存器行为
  （[RM0522](https://www.st.com/resource/en/reference_manual/rm0522-stm32c5-series-armbased-32bit-mcus-stmicroelectronics.pdf)）。
- STM32CubeC5 2.0.0，首个官方版本发布于 2026-03-13
  （[release notes](https://dev.st.com/stm32cube-docs/stm32c5-mcu-package/2.1.0/en/release_notes.html)）；
  STM32C5xx DFP 2.1.0，发布于 2026-06-12
  （[release notes](https://dev.st.com/stm32cube-docs/stm32c5xx-dfp/2.1.0/en/release_notes.html)）。
- ESP-AT 自定义 BLE 服务使用
  `customized_partitions/raw_data/ble_data/gatts_data.csv`
  （[ESP-AT 指南](https://docs.espressif.com/projects/esp-at/en/latest/esp32/Compile_and_Develop/How_to_customize_BLE_services.html)）。

## 许可证

MIT，版权 2026 platojobs。SPDX-License-Identifier: `MIT`。见
[LICENSE](LICENSE)。`firmware/vendor/` 下的 CMSIS 与 STM32C5xx 设备文件保留其
原始许可证。
