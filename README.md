# 声知腕表 - 基于 openvela 的环境声音感知终端

## 一、作品简介

**声知腕表**是一款面向听障人士及需要专注工作的用户的环境声音感知终端。通过端侧识别火灾警报、汽车鸣笛、门铃和人员呼叫等关键环境声音，并以振动和屏幕信息及时提醒，解决用户无法察觉重要声音及潜在危险的问题。

**核心亮点：**
- 端侧实时声音识别，无需联网
- 分级振动提醒，不同声音不同模式
- LVGL 图形界面，直观显示识别结果
- 事件存储与历史查询
- BLE 蓝牙手机配置

## 二、选题方向

**AI 硬件产品创新**

本项目基于 openvela 操作系统，在思澈科技 SF32LB52-DevKit-LCD 开发板上实现完整的环境声音感知功能。通过端侧 AI 推理，实现低延迟、高可靠的声音识别，无需依赖云端服务。

## 三、目录结构

```text
apps/soundwatch/                # SoundWatch 主应用
├── src/main.c                  # 主程序入口与状态机
├── include/soundwatch.h        # 公共头文件与类型定义
└── components/
    ├── audio/audio.c           # 音频采集模块（模拟模式）
    ├── recognition/recognition.c # 声音识别模块（DFT + 预设模型）
    ├── vibration/vibration.c   # 振动提醒模块（线程化）
    ├── ui/ui.c                 # LVGL 图形界面模块
    ├── storage/storage.c       # 事件存储模块（环形缓冲区）
    └── bluetooth/bluetooth.c   # BLE 蓝牙配置模块

app/hello_app/                  # 示例应用
board/contest_board/            # 板级适配代码
quickapp/hello_quickapp/        # 快应用示例
logs/                           # AI Coding 日志
docs/                           # 开发文档与计划
```

## 四、运行方式

### 环境要求

- Ubuntu 22.04 LTS
- ARM GCC 13.4.0+
- CMake 3.22+ / Ninja 1.10+
- openvela SDK

### 编译命令

```bash
# 拉取完整工程
repo init -u https://github.com/open-vela/contest2026_419_haidixiaozongdui \
  -b dev-ai-contest-2026 -m contest2026_419_haidixiaozongdui.xml
repo sync -c -j8

# 进入 openvela 工作区根目录
cd ..

# 配置
cmake -B cmake_out/sf32lb52_devkit_lcd -S nuttx -GNinja \
  -DBOARD_CONFIG=../vendor/sifli/boards/sf32lb52/sf32lb52_devkit_lcd/configs/nsh

# 编译
ninja -C cmake_out/sf32lb52_devkit_lcd
```

### 烧录命令

```bash
sftool -c SF32LB52 -p /dev/ttyUSB0 -b 1000000 \
       --before default_reset --after soft_reset \
       write_flash cmake_out/sf32lb52_devkit_lcd/nuttx.bin@0x12010000
```

## 五、技术架构

```text
PDM/I2S 麦克风
       │
       ▼
音频采集与预处理 ──> 特征提取(DFT) ──> 声音分类(预设模型)
       │                                       │
       │                                       ├─> 事件类型 + 置信度
       ▼                                       │
降噪/分帧/归一化 ─────────────────────────────┘
                                            │
                                            ▼
                                ┌─ 振动提醒(PWM/线程)
                                ├─ 屏幕提醒(LVGL)
                                ├─ 蓝牙同步(BLE GATT)
                                └─ 本地存储(文件系统)
```

## 六、AI Coding 使用说明

本项目借助 AI 辅助开发，在以下环节发挥了重要作用：

### 需求分析与方案设计
- 分析 SF32LB52 开发板硬件能力
- 设计模块化架构方案
- 制定开发计划与风险评估

### 编码实现
- 实现 DFT 特征提取算法
- 设计预设模型权重
- 实现线程化振动提醒
- 添加 JSON 安全转义
- 修复资源泄漏与线程安全问题

### 代码审查与修复
- 发现并修复 5 个严重问题
- 修复 6 个中等问题
- 统一队伍编号（000 → 419）
- 完善文档与配置

### AI 辅助效果
- **开发效率提升 3-5 倍**：快速生成代码框架
- **代码质量保证**：系统性审查发现潜在问题
- **文档自动化**：自动生成 README 与注释

**完整对话日志见 `logs/` 目录**

## 七、参考文档

- [SF32LB52-DevKit-LCD 官方 Wiki](https://wiki.sifli.com/board/sf32lb52x/SF32LB52-DevKit-LCD.html)
- [openvela vendor_sifli 官方开发指南](https://github.com/open-vela/vendor_sifli/blob/dev-ai-contest-2026/boards/sf32lb52/sf32lb52_devkit_lcd/README_zh-cn.md)
- [openvela 大赛总览](https://github.com/open-vela/docs/blob/dev-ai-contest-2026/zh-cn/contest_2026/contest_overview.md)
- [CMSIS-DSP 官方文档](https://arm-software.github.io/CMSIS-DSP/latest/)

## 八、许可协议

Apache-2.0
