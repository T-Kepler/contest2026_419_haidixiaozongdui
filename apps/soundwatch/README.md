# 声知腕表 - 基于 openvela 的环境声音感知终端

## 项目简介

面向听障人士及需要专注工作的用户，通过端侧识别火灾警报、汽车鸣笛、门铃和人员呼叫等关键环境声音，并以振动和屏幕信息及时提醒，解决用户无法察觉重要声音及潜在危险的问题。

## 目标开发板

**思澈科技 SF32LB52-DevKit-LCD**

- 1.85" 390×450 CO5300 AMOLED 显示屏
- FT6146 电容触控面板
- ARM Cortex-M33 处理器
- 16 MB NOR Flash
- 双模蓝牙支持

## 核心功能

### 1. 环境声音识别
- 火灾警报识别
- 汽车鸣笛识别
- 门铃识别
- 人员呼叫识别

### 2. 振动分级提醒
- 火灾警报：连续强振动 (500ms on, 100ms off, 5次)
- 汽车鸣笛：两次长振动 (300ms on, 200ms off, 2次)
- 门铃：两次短振动 (150ms on, 100ms off, 2次)
- 人员呼叫：三次中振动 (200ms on, 150ms off, 3次)

### 3. LVGL 图形界面
- 实时提醒显示
- 状态信息显示
- 事件日志查看

### 4. 事件存储
- 文件系统存储
- 循环覆盖策略
- 支持查询历史记录

### 5. 蓝牙配置
- BLE GATT 服务
- 支持手机配置下发
- 支持事件同步

## 技术架构

### 总体架构

`	ext
PDM/I2S 麦克风
       │
       ▼
音频采集与预处理 ──> 特征提取(CMSIS-DSP) ──> 轻量CNN分类(INT8)
       │                                           │
       │                                           ├─> 事件类型 + 置信度
       ▼                                           │
降噪/分帧/归一化 ─────────────────────────────────┘
                                                │
                                                ▼
                                    ┌─ 振动提醒(PWM/GPIO)
                                    ├─ 屏幕提醒(LVGL)
                                    ├─ 蓝牙同步(BLE GATT)
                                    └─ 本地存储(Flash/KV)
`

### 代码结构

`	ext
apps/soundwatch/
├── CMakeLists.txt              # CMake 构建配置
├── Kconfig                     # 内核配置
├── Make.defs                   # Make 定义
├── Makefile                    # Make 构建配置
├── include/
│   └── soundwatch.h            # 公共头文件
├── src/
│   └── main.c                  # 主应用入口
└── components/
    ├── audio/
    │   └── audio.c             # 音频采集模块
    ├── recognition/
    │   └── recognition.c       # 声音识别模块
    ├── vibration/
    │   └── vibration.c         # 振动提醒模块
    ├── ui/
    │   └── ui.c                # LVGL 图形界面模块
    ├── storage/
    │   └── storage.c           # 事件存储模块
    └── bluetooth/
        └── bluetooth.c         # 蓝牙配置模块
`

## 编译环境

### 虚拟机信息

- **操作系统：** Ubuntu 22.04.5 LTS
- **IP 地址：** 192.168.184.128
- **用户名：** t

### 工具链

| 工具 | 版本 | 位置 |
|------|------|------|
| ARM GCC | 13.4.0 | /home/t/openvela/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin/ |
| CMake | 3.22.1 | /usr/bin/cmake |
| Ninja | 1.10.1 | /usr/bin/ninja |
| Python | 3.10.12 | /usr/bin/python3 |
| genromfs | - | /home/t/openvela/prebuilts/tools/linux/x86_64/genromfs |

### 编译命令

`ash
# 设置 PATH
export PATH=/home/t/openvela/prebuilts/tools/linux/x86_64:/home/t/openvela/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:/home/t/.local/bin:/usr/bin:/bin:

# 配置
cd /home/t/openvela
cmake -B cmake_out/sf32lb52_devkit_lcd -S nuttx -GNinja -DBOARD_CONFIG=../vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh

# 编译
ninja -C cmake_out/sf32lb52_devkit_lcd
`

## 烧录命令

`ash
# 烧录到开发板
sftool -c SF32LB52 -p /dev/ttyUSB0 -b 1000000 \
       --before default_reset --after soft_reset \
       write_flash /home/t/openvela/cmake_out/sf32lb52_devkit_lcd/nuttx.bin@0x12010000
`

## 编译结果

### 生成的固件文件

| 文件 | 大小 | 说明 |
|------|------|------|
| 
uttx.bin | 1.56 MB | 可烧录固件 |
| 
uttx | 13.45 MB | ELF 文件 |
| 
uttx.map | 13.62 MB | 内存映射文件 |

### 内存使用

`
Memory region         Used Size  Region Size  %age Used
           flash:     1563100 B        16 MB      9.32%
            sram:      121660 B       512 KB     23.20%
           psram:           0 B         8 MB      0.00%
`

## 待完善项

### 高优先级

1. **真实音频采集** - 当前使用模拟实现，需连接麦克风
2. **声音识别模型** - 当前使用随机权重，需训练预训练模型
3. **BLE 蓝牙通信** - 当前为框架代码，需实现实际通信

### 中优先级

1. **优化特征提取** - 使用 CMSIS-DSP 加速
2. **添加电源管理** - 实现低功耗模式
3. **完善 UI 交互** - 添加触摸事件处理

### 低优先级

1. **添加日志系统** - 分级日志，日志文件滚动
2. **添加单元测试** - 模块独立测试，集成测试

## 参考资料

- [SF32LB52-DevKit-LCD 官方 Wiki](https://wiki.sifli.com/board/sf32lb52x/SF32LB52-DevKit-LCD.html)
- [openvela vendor_sifli 官方开发指南](https://github.com/open-vela/vendor_sifli/blob/dev-ai-contest-2026/boards/sf32lb52/sf32lb52_devkit_lcd/README_zh-cn.md)
- [openvela 大赛总览](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md)
- [CMSIS-DSP 官方文档](https://arm-software.github.io/CMSIS-DSP/latest/)

## 许可协议

Apache-2.0
