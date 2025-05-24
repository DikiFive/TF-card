/*
 * ESP32-S3 SD卡读写速度测试示例
 *
 * 本示例演示了如何在ESP32-S3上使用SDMMC外设读写SD卡，并测试读写速度。
 * 主要功能：
 * 1. SD卡初始化和FAT文件系统挂载
 * 2. 基本文件操作（创建、写入、重命名、读取）
 * 3. SD卡读写速度测试（可配置测试文件大小和缓冲区大小）
 *
 * 注意事项：
 * - 请确保SD卡已正确插入
 * - 需要在menuconfig中正确配置SD卡的GPIO引脚
 * - 建议在SD卡信号线上添加10K上拉电阻
 * - 支持1线和4线模式（可在menuconfig中配置）
 *
 * 作者：OpenAI
 * 本示例代码属于公共领域（或根据您的选择使用CC0许可）。
 *
 * 除非适用法律要求或书面同意，本软件按"原样"分发，
 * 不附带任何明示或暗示的担保或条件。
 */

// 本示例使用SDMMC外设与SD卡通信，支持标准SD卡和SDHC/SDXC卡

// 包含字符串操作相关函数
#include <string.h>
// 包含POSIX标准的文件操作函数
#include <sys/unistd.h>
// 包含文件状态相关结构和函数
#include <sys/stat.h>
// 包含ESP32 FAT文件系统相关函数
#include "esp_vfs_fat.h"
// 包含SD/MMC卡命令相关函数
#include "sdmmc_cmd.h"
// 包含SD/MMC主机驱动相关函数
#include "driver/sdmmc_host.h"
// 包含高精度计时器相关函数
#include "esp_timer.h"
// 包含FreeRTOS相关函数
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <errno.h>

// 定义SD卡在虚拟文件系统中的挂载点
#define MOUNT_POINT "/sdcard"

// 定义SD卡读写速度测试相关参数
#define TEST_BUFFER_SIZE (128 * 1024)          // 每次读写的缓冲区大小：128KB（提升读写性能）
#define TEST_FILE_SIZE (4 * 1024 * 1024)       // 测试文件总大小：4MB（增大文件以获得更准确的速度测试）
#define TEST_FILE_PATH MOUNT_POINT "/test.txt" // 测试文件路径（使用.txt扩展名避免兼容性问题）

// 定义日志标签
static const char *TAG = "example";

/**
 * @brief SD卡写入速度测试函数
 *
 * 该函数通过以下步骤测试SD卡的写入速度：
 * 1. 创建一个指定大小(TEST_BUFFER_SIZE)的缓冲区
 * 2. 使用规律数据填充缓冲区
 * 3. 创建测试文件并打开
 * 4. 通过多次写入缓冲区数据，直到达到指定的测试文件大小(TEST_FILE_SIZE)
 * 5. 使用高精度计时器计算写入速度
 *
 * 注意：
 * - 函数会先检查并删除已存在的测试文件
 * - 写入完成后会执行fsync确保数据真正写入到SD卡
 * - 如果分配缓冲区失败或文件操作失败，函数会提前返回
 */
static void test_write_speed(void)
{
    ESP_LOGI(TAG, "Testing write speed...");

    // 检查并删除可能存在的旧测试文件
    struct stat st;
    if (stat(TEST_FILE_PATH, &st) == 0)
    {
        unlink(TEST_FILE_PATH);
    }

    // 创建测试数据缓冲区
    uint8_t *buffer = malloc(TEST_BUFFER_SIZE);
    if (buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return;
    }
    // 填充缓冲区
    for (int i = 0; i < TEST_BUFFER_SIZE; i++)
    {
        buffer[i] = i & 0xFF;
    }

    // 创建测试文件
    ESP_LOGI(TAG, "Opening file for writing: %s", TEST_FILE_PATH);
    FILE *f = fopen(TEST_FILE_PATH, "w");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for writing (errno: %d, path: %s)", errno, TEST_FILE_PATH);
        free(buffer);
        return;
    }

    // 开始计时
    int64_t start_time = esp_timer_get_time();

    // 写入测试数据
    size_t bytes_written = 0;
    while (bytes_written < TEST_FILE_SIZE)
    {
        size_t to_write = TEST_FILE_SIZE - bytes_written;
        if (to_write > TEST_BUFFER_SIZE)
        {
            to_write = TEST_BUFFER_SIZE;
        }
        size_t written = fwrite(buffer, 1, to_write, f);
        if (written != to_write)
        {
            ESP_LOGE(TAG, "Write failed");
            break;
        }
        bytes_written += written;
    }

    // 确保数据写入到卡上
    fflush(f);
    fsync(fileno(f));
    fclose(f);

    // 计算写入速度
    int64_t end_time = esp_timer_get_time();
    float time_s = (end_time - start_time) / 1000000.0;
    float speed_mb = (TEST_FILE_SIZE / (1024.0 * 1024.0)) / time_s;

    ESP_LOGI(TAG, "Write speed: %.2f MB/s (%.2f seconds for %d bytes)",
             speed_mb, time_s, TEST_FILE_SIZE);

    free(buffer);
}

/**
 * @brief SD卡读取速度测试函数
 *
 * 该函数通过以下步骤测试SD卡的读取速度：
 * 1. 创建一个指定大小(TEST_BUFFER_SIZE)的缓冲区
 * 2. 打开由写入测试创建的文件
 * 3. 循环读取文件内容到缓冲区，直到读取完整个文件(TEST_FILE_SIZE)
 * 4. 使用高精度计时器计算读取速度
 *
 * 注意：
 * - 读取完成后会删除测试文件
 * - 如果分配缓冲区失败或文件操作失败，函数会提前返回
 * - 此函数应该在test_write_speed之后调用
 */
static void test_read_speed(void)
{
    ESP_LOGI(TAG, "Testing read speed...");

    // 创建DMA兼容的读取缓冲区
    uint8_t *buffer = heap_caps_malloc(TEST_BUFFER_SIZE, MALLOC_CAP_DMA);
    if (buffer == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        return;
    }

    // 打开测试文件
    ESP_LOGI(TAG, "Opening file for reading: %s", TEST_FILE_PATH);
    FILE *f = fopen(TEST_FILE_PATH, "r");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading (errno: %d, path: %s)", errno, TEST_FILE_PATH);
        free(buffer);
        return;
    }

    // 开始计时
    int64_t start_time = esp_timer_get_time();

    // 读取测试数据
    size_t bytes_read = 0;
    while (bytes_read < TEST_FILE_SIZE)
    {
        size_t to_read = TEST_FILE_SIZE - bytes_read;
        if (to_read > TEST_BUFFER_SIZE)
        {
            to_read = TEST_BUFFER_SIZE;
        }

        size_t read = fread(buffer, 1, to_read, f);
        if (read != to_read)
        {
            ESP_LOGE(TAG, "Read partial/failed: read=%d, expected=%d, bytes_read_total=%d, ferror=%d, feof=%d",
                     (int)read, (int)to_read, (int)bytes_read, ferror(f), feof(f));
            if (ferror(f))
            {
                ESP_LOGE(TAG, "Read failed with error %d", errno);
            }
            else
            {
                ESP_LOGW(TAG, "Unexpected EOF at %d bytes", (int)bytes_read);
            }
            break;
        }
        // 每次成功读取后，打印进度
        ESP_LOGD(TAG, "Read %d bytes, total %d/%d", (int)read, (int)bytes_read + (int)read, (int)TEST_FILE_SIZE);
        bytes_read += read;
    }
    fclose(f);

    // 计算读取速度
    int64_t end_time = esp_timer_get_time();
    float time_s = (end_time - start_time) / 1000000.0;
    float speed_mb = (TEST_FILE_SIZE / (1024.0 * 1024.0)) / time_s;

    ESP_LOGI(TAG, "Read speed: %.2f MB/s (%.2f seconds for %d bytes)",
             speed_mb, time_s, TEST_FILE_SIZE);

    free(buffer);

    // 删除测试文件
    unlink(TEST_FILE_PATH);
}

/**
 * @brief 主程序入口函数
 *
 * 程序主要流程：
 * 1. 配置并初始化SD卡
 *    - 设置FAT文件系统参数
 *    - 配置SDMMC主机和GPIO
 *    - 挂载文件系统
 *
 * 2. 执行基本文件操作测试
 *    - 创建并写入hello.txt
 *    - 重命名为foo.txt
 *    - 读取文件内容
 *
 * 3. 执行SD卡速度测试
 *    - 写入速度测试
 *    - 读取速度测试
 *
 * 4. 清理并卸载
 *    - 删除测试文件
 *    - 卸载文件系统
 *
 * 注意：函数会自动处理各种错误情况，
 * 如挂载失败、文件操作失败等，并通过
 * ESP_LOG宏输出详细的错误信息。
 */
void app_main(void)
{
    // 用于存储函数返回值的错误码
    esp_err_t ret;

    // 文件系统挂载配置选项
    // 如果format_if_mount_failed设置为true，则在挂载失败时
    // 会对SD卡进行分区和格式化操作
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
#ifdef CONFIG_EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .format_if_mount_failed = true, // 挂载失败时格式化SD卡
#else
        .format_if_mount_failed = false, // 挂载失败时不格式化SD卡
#endif                                      // EXAMPLE_FORMAT_IF_MOUNT_FAILED
        .max_files = 5,                     // 最大同时打开文件数
        .allocation_unit_size = 32 * 1024}; // FAT文件系统分配单元大小(32KB，优化文件系统性能)

    // SD卡信息结构体指针
    sdmmc_card_t *card;
    // 文件系统挂载点
    const char mount_point[] = MOUNT_POINT;
    // 输出日志：开始初始化SD卡
    ESP_LOGI(TAG, "Initializing SD card");

    // 使用上面定义的设置来初始化SD卡并挂载FAT文件系统
    // 注意：esp_vfs_fat_sdmmc/sdspi_mount是集成了所有功能的便捷函数
    // 在开发生产应用时，请查看其源代码并实现错误恢复机制

    // 输出日志：使用SDMMC外设
    ESP_LOGI(TAG, "Using SDMMC peripheral");
    // 获取SDMMC主机默认配置
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    // 设置SDMMC时钟频率为40MHz以提高性能
    host.max_freq_khz = 40000;

    // 初始化SD卡插槽配置，这里不使用卡检测(CD)和写保护(WP)信号
    // 如果您的开发板上有这些信号，请修改slot_config.gpio_cd和slot_config.gpio_wp
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    // 设置SD卡为1线模式，因为硬件只支持1线
    slot_config.width = 1; // 1线模式 (仅使用DAT0)

    // 在支持配置SD卡GPIO的芯片上（如ESP32S3），
    // 在slot_config结构体中设置这些引脚：
#ifdef CONFIG_IDF_TARGET_ESP32S3
    slot_config.clk = CONFIG_EXAMPLE_PIN_CLK; // 时钟信号引脚
    slot_config.cmd = CONFIG_EXAMPLE_PIN_CMD; // 命令信号引脚
    slot_config.d0 = CONFIG_EXAMPLE_PIN_D0;   // 数据线0引脚
#ifdef CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4
    slot_config.d1 = CONFIG_EXAMPLE_PIN_D1; // 数据线1引脚
    slot_config.d2 = CONFIG_EXAMPLE_PIN_D2; // 数据线2引脚
    slot_config.d3 = CONFIG_EXAMPLE_PIN_D3; // 数据线3引脚
#endif                                      // CONFIG_EXAMPLE_SDMMC_BUS_WIDTH_4
#endif                                      // CONFIG_IDF_TARGET_ESP32S3

    // 在使能的引脚上启用内部上拉电阻
    // 但是内部上拉电阻的强度不够，请确保在总线上
    // 连接10k的外部上拉电阻。这仅用于调试/示例目的。
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    // 输出日志：开始挂载文件系统
    ESP_LOGI(TAG, "Mounting filesystem");
    // 调用esp_vfs_fat_sdmmc_mount函数挂载SD卡
    ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) // 如果挂载失败
    {
        if (ret == ESP_FAIL) // 如果是文件系统挂载失败
        {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                          "If you want the card to be formatted, set the EXAMPLE_FORMAT_IF_MOUNT_FAILED menuconfig option.");
            // 输出错误日志：文件系统挂载失败。如果要格式化卡，请设置EXAMPLE_FORMAT_IF_MOUNT_FAILED菜单选项
        }
        else // 如果是其他错误（如硬件初始化失败）
        {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). "
                          "Make sure SD card lines have pull-up resistors in place.",
                     esp_err_to_name(ret));
            // 输出错误日志：卡初始化失败。请确保SD卡信号线上有上拉电阻
        }
        return; // 发生错误，退出函数
    }
    // 输出日志：文件系统挂载成功
    ESP_LOGI(TAG, "Filesystem mounted");

    // SD卡已初始化，打印其属性信息（如容量、制造商等）
    sdmmc_card_print_info(stdout, card);

    // 使用POSIX和C标准库函数操作文件：

    // 首先创建一个文件
    const char *file_hello = MOUNT_POINT "/hello.txt";

    // 输出日志：正在打开文件
    ESP_LOGI(TAG, "Opening file %s", file_hello);
    // 以写入模式打开文件
    FILE *f = fopen(file_hello, "w");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for writing"); // 输出错误日志：打开文件失败
        return;
    }
    // 向文件写入一行文本，包含SD卡的名称
    fprintf(f, "Hello %s!\n", card->cid.name);
    fclose(f);                     // 关闭文件
    ESP_LOGI(TAG, "File written"); // 输出日志：文件写入完成

    // 定义第二个文件路径
    const char *file_foo = MOUNT_POINT "/foo.txt";

    // 重命名文件前检查目标文件是否存在
    struct stat st;
    if (stat(file_foo, &st) == 0)
    {
        // 如果目标文件存在，则删除它
        unlink(file_foo);
    }

    // 重命名原始文件
    ESP_LOGI(TAG, "Renaming file %s to %s", file_hello, file_foo);
    if (rename(file_hello, file_foo) != 0)
    {
        ESP_LOGE(TAG, "Rename failed"); // 输出错误日志：重命名失败
        return;
    }

    // 以读取模式打开重命名后的文件
    ESP_LOGI(TAG, "Reading file %s", file_foo);
    f = fopen(file_foo, "r");
    if (f == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for reading"); // 输出错误日志：打开文件失败
        return;
    }

    // 从文件中读取一行内容
    char line[64];                // 定义64字节的缓冲区
    fgets(line, sizeof(line), f); // 读取一行文本
    fclose(f);                    // 关闭文件

    // 去除换行符
    char *pos = strchr(line, '\n');
    if (pos)
    {
        *pos = '\0'; // 将换行符替换为字符串结束符
    }
    // 输出日志：显示从文件读取的内容
    ESP_LOGI(TAG, "Read from file: '%s'", line);

    // 执行SD卡速度测试
    test_write_speed();
    test_read_speed();
    esp_vfs_fat_sdcard_unmount(mount_point, card);
    // 输出日志：SD卡已卸载
    ESP_LOGI(TAG, "Card unmounted");
}
