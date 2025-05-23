# ESP32-S3 SD卡读写速度测试

这个项目使用ESP32-S3的SDMMC外设测试SD卡的读写速度。项目基于ESP-IDF框架开发，支持标准SD卡和SDHC/SDXC卡。

## 功能特点

- SD卡初始化和FAT文件系统挂载
- 基本文件操作演示（创建、写入、重命名、读取）
- 支持1线和4线SD卡通信模式
- SD卡读写速度测试（可配置测试文件大小）
- 详细的错误处理和日志输出

## 硬件要求

- ESP32-S3开发板
- SD卡（支持标准SD卡、SDHC和SDXC）
- SD卡槽或SD卡转接板
- 10K上拉电阻（建议在所有SD卡信号线上使用）

## 引脚连接

| SD卡引脚 | ESP32-S3引脚 | 说明 |
|---------|-------------|------|
| CLK     | GPIO36      | 时钟信号 |
| CMD     | GPIO35      | 命令信号 |
| D0      | GPIO37      | 数据线0 |
| D1      | GPIO38      | 数据线1（4线模式） |
| D2      | GPIO33      | 数据线2（4线模式） |
| D3      | GPIO34      | 数据线3（4线模式） |

注意：以上引脚分配可以在menuconfig中修改。

## 快速开始

### 编译和烧录

1. 安装ESP-IDF（推荐v4.4.4或更新版本）
2. 克隆仓库并进入项目目录
3. 配置项目：
   ```bash
   idf.py menuconfig
   ```
   在 `Example Configuration` 菜单中：
   - 设置SD卡的GPIO引脚
   - 选择1线或4线模式
   - 配置是否在挂载失败时格式化SD卡

4. 编译和烧录：
   ```bash
   idf.py build flash monitor
   ```

### 测试结果示例

```
I (407) example: Filesystem mounted
Name: SD16G
Type: SDHC/SDXC
Speed: 20 MHz
Size: 3840MB
I (957) example: Write speed: 1.72 MB/s (0.07 seconds for 131072 bytes)
I (1037) example: Read speed: 0.51 MB/s (0.25 seconds for 131072 bytes)
```

## 配置说明

### 主要配置参数

在 `main/sd_card_example_main.c` 中：
```c
// 缓冲区和文件大小配置
#define TEST_BUFFER_SIZE (32 * 1024)     // 每次读写的缓冲区大小：32KB
#define TEST_FILE_SIZE (1024 * 1024)     // 测试文件总大小：1MB
```

### Menuconfig选项

- `Example Configuration`
  - `Format the card if mount failed` - 挂载失败时是否格式化
  - `SD/MMC bus width` - 选择1线或4线模式
  - `CLK GPIO number` - 时钟信号引脚
  - `CMD GPIO number` - 命令信号引脚
  - `D0 GPIO number` - 数据线0引脚
  - `D1 GPIO number` - 数据线1引脚（4线模式）
  - `D2 GPIO number` - 数据线2引脚（4线模式）
  - `D3 GPIO number` - 数据线3引脚（4线模式）

## 故障排除

### 常见问题及解决方法

1. 文件操作失败 (errno: 22)
   - 原因：之前使用二进制模式打开文件导致兼容性问题
   - 解决：使用文本模式打开文件（"w"/"r"），已在代码中修复

2. SD卡初始化失败
   - 检查SD卡是否正确插入
   - 确认所有信号线上有10K上拉电阻
   - 验证GPIO配置是否正确

3. 速度测试性能优化
   - 使用4线模式可显著提高传输速度
   - 增大TEST_BUFFER_SIZE可提高大文件传输效率
   - 确保电源供应稳定，建议使用外部3.3V供电

### 调试方法

1. 查看详细日志：
   ```bash
   idf.py monitor
   ```

2. 检查SD卡信息：
   程序会自动打印SD卡的详细信息，包括：
   - 卡名称和类型
   - 通信速度
   - 容量大小

## 已知限制

- 文件大小限制：建议不要超过1MB，以避免内存问题
- SD卡兼容性：某些低速卡可能需要降低通信速度
- GPIO限制：部分GPIO不适合用作SD卡接口，请参考硬件指南

## 后续改进计划

1. 添加更多测试模式：
   - 随机读写测试
   - 多文件并发测试
   - 长时间稳定性测试

2. 性能优化：
   - DMA传输支持
   - 缓存优化
   - 多线程支持

## 许可证

本项目代码属于公共领域（或根据您的选择使用CC0许可）。
