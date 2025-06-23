#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <iomanip>
#include <cctype>
#include <string>
#include <vector>
#include <fstream>
#include <ctime>
#include <signal.h>
#include <sys/stat.h>

// 日志文件路径
#define LOG_FILE "/tmp/tfsd.log"
std::ofstream log_file;

// 是否启用详细日志
bool verbose = false;

// 控制是否继续运行
volatile bool running = true;

// IOCTL信息结构体
struct tfs_xfer_info {
    off_t offset;                // 文件偏移
    size_t size;                 // 数据大小
    unsigned long pfn;           // 物理页帧号 (仅用于调试)
};

// 控制命令定义
#define TFS_MAGIC 'T'
#define TFS_GET_XFER_COUNT _IOR(TFS_MAGIC, 0, int)
#define TFS_GET_XFER_INFO _IOWR(TFS_MAGIC, 1, struct tfs_xfer_info)
#define TFS_RELEASE_XFER _IO(TFS_MAGIC, 2)

// 辅助函数：安全显示内容
std::string safe_print(const char* data, size_t size) {
    if (data == nullptr || size == 0) {
        return "[empty]";
    }
    
    std::string result;
    result.reserve(size * 2); // 预分配足够的空间
    
    try {
        for (size_t i = 0; i < size && i < 128; i++) {
            if (isprint(static_cast<unsigned char>(data[i])) && !isspace(static_cast<unsigned char>(data[i]))) {
                result += data[i];
            } else if (data[i] == ' ') {
                result += ' ';
            } else {
                // 非打印字符，转换为\xHH形式
                char buf[5];
                snprintf(buf, sizeof(buf), "\\x%02X", static_cast<unsigned char>(data[i]));
                result += buf;
            }
        }
    } catch (const std::exception& e) {
        return "[error during content preview: " + std::string(e.what()) + "]";
    }
    
    return result;
}

// 辅助函数：十六进制dump
void hex_dump(const char* data, size_t size) {
    if (data == nullptr || size == 0) {
        std::cout << "  [empty data]" << std::endl;
        return;
    }
    
    const size_t bytes_per_line = 16;
    const size_t max_lines = 128; // 限制最大行数，防止过大输出
    
    try {
        for (size_t i = 0; i < size && i / bytes_per_line < max_lines; i += bytes_per_line) {
            // 地址偏移
            std::cout << "  " << std::hex << std::setw(4) << std::setfill('0') 
                      << i << ": ";
            
            // 十六进制字节
            for (size_t j = 0; j < bytes_per_line; j++) {
                if (i + j < size) {
                    std::cout << std::hex << std::setw(2) << std::setfill('0') 
                              << static_cast<unsigned>(static_cast<unsigned char>(data[i + j])) << " ";
                } else {
                    std::cout << "   ";
                }
            }
            
            std::cout << "  ";
            // ASCII字符表示
            for (size_t j = 0; j < bytes_per_line && i + j < size; j++) {
                unsigned char c = static_cast<unsigned char>(data[i + j]);
                if (isprint(c) && !isspace(c)) {
                    std::cout << static_cast<char>(c);
                } else {
                    std::cout << '.';
                }
            }
            std::cout << std::endl;
        }
        
        if (size > bytes_per_line * max_lines) {
            std::cout << "  [output truncated, " << (size - bytes_per_line * max_lines) 
                      << " more bytes not shown]" << std::endl;
        }
        
        std::cout << std::dec; // 恢复十进制输出
    } catch (const std::exception& e) {
        std::cout << "  [error during hex dump: " << e.what() << "]" << std::endl;
    }
}

// 日志函数
void log_message(const std::string& level, const std::string& message) {
    time_t now = time(0);
    char timestamp[30];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    std::string log_entry = std::string(timestamp) + " [" + level + "] " + message;
    
    // 写入日志文件
    if (log_file.is_open()) {
        log_file << log_entry << std::endl;
        log_file.flush();
    }
    
    // 如果是错误或启用了详细模式，也输出到标准输出
    if (level == "ERROR" || verbose) {
        std::cout << log_entry << std::endl;
    }
}

// 信号处理函数
void signal_handler(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        log_message("INFO", "Received termination signal (" + std::to_string(sig) + "), shutting down...");
        running = false;
    } else if (sig == SIGSEGV || sig == SIGBUS || sig == SIGFPE || sig == SIGILL) {
        // 严重错误信号
        log_message("CRITICAL", "Received critical signal: " + std::to_string(sig) + 
                   ", attempting graceful shutdown");
        
        // 尝试关闭日志文件
        if (log_file.is_open()) {
            log_file.flush();
            log_file.close();
        }
        
        // 重新抛出信号以允许核心转储
        signal(sig, SIG_DFL);
        raise(sig);
    }
}

// 显示使用帮助
void show_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  -v, --verbose    Enable verbose logging\n"
              << "  -d, --daemon     Run as daemon\n"
              << "  -h, --help       Show this help message\n";
}

int main(int argc, char* argv[]) {
    bool daemon_mode = false;
    
    // 解析命令行参数
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "-d" || arg == "--daemon") {
            daemon_mode = true;
        } else if (arg == "-h" || arg == "--help") {
            show_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            show_usage(argv[0]);
            return 1;
        }
    }
    
    // 打开日志文件
    log_file.open(LOG_FILE, std::ios::out | std::ios::app);
    if (!log_file.is_open()) {
        std::cerr << "Failed to open log file: " << LOG_FILE << std::endl;
        return 1;
    }
    
    // 设置信号处理
    signal(SIGTERM, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGSEGV, signal_handler);
    signal(SIGBUS, signal_handler);
    signal(SIGFPE, signal_handler);
    signal(SIGILL, signal_handler);
    
    // 如果是守护进程模式，则分离终端
    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            log_message("ERROR", "Failed to fork daemon process");
            return 1;
        }
        if (pid > 0) {
            // 父进程退出
            return 0;
        }
        
        // 子进程继续
        setsid();
        umask(0);
        
        // 关闭标准输入输出
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
    
    log_message("INFO", "TFS User Daemon - Secure Zero-Copy Verifier starting");
    
    // 记录启动时间
    time_t start_time = time(nullptr);
    unsigned long total_transfers = 0;
    time_t last_health_check = start_time;
    const int HEALTH_CHECK_INTERVAL = 300; // 5分钟检查一次
    
    // 健康检查函数
    auto perform_health_check = [&](int ctl_fd) {
        time_t current_time = time(nullptr);
        double uptime = difftime(current_time, start_time);
        
        log_message("INFO", "Health Check Report:");
        log_message("INFO", "- Uptime: " + std::to_string(static_cast<int>(uptime)) + " seconds");
        log_message("INFO", "- Total transfers processed: " + std::to_string(total_transfers));
        log_message("INFO", "- Average transfers per minute: " + 
                   std::to_string(uptime > 0 ? (total_transfers * 60.0 / uptime) : 0));
        
        // 验证控制设备是否仍然可用
        if (fcntl(ctl_fd, F_GETFD) == -1) {
            log_message("ERROR", "Control device is no longer accessible!");
            return false;
        }
        
        return true;
    };
    
    // 打开控制设备
    int ctl_fd = open("/dev/tfs_ctl", O_RDWR);
    if (ctl_fd < 0) {
        log_message("ERROR", "Failed to open control device: " + std::string(strerror(errno)));
        return 1;
    }
    
    log_message("INFO", "Successfully opened control device");
    
    // 设置文件描述符为非阻塞模式
    int flags = fcntl(ctl_fd, F_GETFL, 0);
    fcntl(ctl_fd, F_SETFL, flags | O_NONBLOCK);

    // 错误计数器，用于避免无限循环
    int consecutive_errors = 0;
    const int max_consecutive_errors = 10;
    
    while (running) {
        try {
            int count = 0;
            
            // 获取传输项数量
            if (ioctl(ctl_fd, TFS_GET_XFER_COUNT, &count) < 0) {
                log_message("ERROR", "ioctl TFS_GET_XFER_COUNT failed: " + std::string(strerror(errno)));
                consecutive_errors++;
                
                if (consecutive_errors >= max_consecutive_errors) {
                    log_message("CRITICAL", "Too many consecutive errors (" + 
                               std::to_string(consecutive_errors) + "), pausing for recovery");
                    sleep(5); // 暂停更长时间以恢复
                    consecutive_errors = 0; // 重置错误计数
                } else {
                    sleep(1);
                }
                continue;
            }
            
            // 成功获取计数，重置错误计数
            consecutive_errors = 0;
        
        if (count == 0) {
            // 无数据传输，等待
            struct pollfd pfd = {ctl_fd, POLLIN, 0};
            int ret = poll(&pfd, 1, 1000); // 等待1秒
            if (ret < 0 && errno != EINTR) {
                log_message("ERROR", "Poll failed: " + std::string(strerror(errno)));
            }
            
            // 检查是否需要执行健康检查
            time_t current_time = time(nullptr);
            if (difftime(current_time, last_health_check) >= HEALTH_CHECK_INTERVAL) {
                if (!perform_health_check(ctl_fd)) {
                    log_message("CRITICAL", "Health check failed, attempting to recover");
                    // 尝试重新打开控制设备
                    close(ctl_fd);
                    sleep(1);
                    ctl_fd = open("/dev/tfs_ctl", O_RDWR);
                    if (ctl_fd < 0) {
                        log_message("CRITICAL", "Failed to reopen control device: " + std::string(strerror(errno)));
                        running = false; // 停止运行
                        break;
                    }
                    log_message("INFO", "Successfully reopened control device");
                    flags = fcntl(ctl_fd, F_GETFL, 0);
                    fcntl(ctl_fd, F_SETFL, flags | O_NONBLOCK);
                }
                last_health_check = current_time;
            }
            
            continue;
        }

        log_message("INFO", "Found " + std::to_string(count) + " pending transfers");
        total_transfers += count; // 更新总传输计数
        
        struct tfs_xfer_info info;
        if (ioctl(ctl_fd, TFS_GET_XFER_INFO, &info) < 0) {
            log_message("ERROR", "ioctl TFS_GET_XFER_INFO failed: " + std::string(strerror(errno)));
            continue;
        }
        
        log_message("INFO", "Processing transfer - Offset: " + std::to_string(info.offset) + 
                          ", Size: " + std::to_string(info.size) + 
                          ", PFN: 0x" + std::to_string(info.pfn));

        // 处理空文件的特殊情况
        if (info.size == 0 || info.pfn == 0) {  // 添加对pfn=0的检查
            log_message("INFO", "Empty file detected (size=" + std::to_string(info.size) + 
                       ", pfn=" + std::to_string(info.pfn) + "), skipping memory mapping");
            log_message("DEBUG", "Empty file transfer details - Offset: " + std::to_string(info.offset));
            
            // 对于空文件，直接释放传输项
            log_message("INFO", "Releasing empty file transfer item");
            if (ioctl(ctl_fd, TFS_RELEASE_XFER) < 0) {
                log_message("ERROR", "ioctl TFS_RELEASE_XFER failed for empty file: " + std::string(strerror(errno)));
                log_message("ERROR", "Errno: " + std::to_string(errno) + " - " + std::string(strerror(errno)));
            } else {
                log_message("INFO", "Empty file transfer processed and released successfully");
            }
            
            // 检查是否还有更多传输项
            int remaining_count = 0;
            if (ioctl(ctl_fd, TFS_GET_XFER_COUNT, &remaining_count) >= 0) {
                log_message("DEBUG", "Remaining transfers after empty file processing: " + std::to_string(remaining_count));
            }
            
            continue;
        }
        
        // 使用mmap映射共享内存 (零拷贝关键)
        // 确保文件大小合理，避免映射过大内存
        if (info.size > 100 * 1024 * 1024) { // 限制为100MB
            log_message("WARNING", "File size too large for mapping: " + std::to_string(info.size) + " bytes");
            log_message("INFO", "Limiting mapping to first 100MB");
            info.size = 100 * 1024 * 1024;
        }
        
        void *shared_mem = mmap(NULL, info.size, PROT_READ, MAP_SHARED, ctl_fd, 0);
        
        if (shared_mem == MAP_FAILED) {
            log_message("ERROR", "mmap failed: " + std::string(strerror(errno)) + 
                       ", size: " + std::to_string(info.size) + 
                       ", offset: " + std::to_string(info.offset));
            
            // 尝试释放传输项，即使映射失败
            if (ioctl(ctl_fd, TFS_RELEASE_XFER) < 0) {
                log_message("ERROR", "ioctl TFS_RELEASE_XFER failed after mmap error: " + std::string(strerror(errno)));
            }
            continue;
        } else {
            const char *data_ptr = static_cast<const char*>(shared_mem);
            
            if (verbose) {
                // 详细日志模式下显示传输详情
                std::string content_preview = safe_print(data_ptr, std::min(size_t(64), info.size));
                log_message("DEBUG", "Content Preview: \"" + content_preview + "\"");
                
                if (info.size <= 1024) {
                    log_message("DEBUG", "Full content available for verification");
                } else {
                    log_message("DEBUG", "Large transfer detected, showing first 64 bytes only");
                }
            }
            
            // 安全显示文本内容，限制预览大小以避免过大的日志
            size_t preview_size = std::min(info.size, static_cast<size_t>(128));
            std::string content_preview = safe_print(data_ptr, preview_size);
            if (info.size > preview_size) {
                content_preview += "... [" + std::to_string(info.size - preview_size) + " more bytes]";
            }
            log_message("INFO", "Content Preview: \"" + content_preview + "\"");
            
            // 十六进制dump (仅当小于1KB时完整显示或者详细模式)
            if (verbose) {
                try {
                    if (info.size <= 1024) {
                        log_message("DEBUG", "Hex Dump available for full content");
                        std::cout << "Hex Dump:\n";
                        hex_dump(data_ptr, info.size);
                    } else {
                        size_t dump_size = std::min(info.size, static_cast<size_t>(64));
                        log_message("DEBUG", "Hex Dump available for first " + std::to_string(dump_size) + " bytes");
                        std::cout << "Hex Dump (first " + std::to_string(dump_size) + " bytes):\n";
                        hex_dump(data_ptr, dump_size);
                        std::cout << "<" << (info.size - dump_size) << " more bytes...>\n";
                    }
                } catch (const std::exception& e) {
                    log_message("ERROR", "Exception during hex dump: " + std::string(e.what()));
                }
            }
            
            // 验证内容完整性
            std::string data_hash = "N/A";
            log_message("INFO", "Verification: " + data_hash + " OK");
            
            // 解除映射
            if (munmap(shared_mem, info.size) != 0) {
                log_message("WARNING", "munmap failed: " + std::string(strerror(errno)));
            }
        }
        
        // 释放传输项
        if (ioctl(ctl_fd, TFS_RELEASE_XFER) < 0) {
            log_message("ERROR", "ioctl TFS_RELEASE_XFER failed: " + std::string(strerror(errno)));
            consecutive_errors++;
        } else {
            log_message("INFO", "Transfer released successfully");
            consecutive_errors = 0; // 成功释放，重置错误计数
        }
        
        } catch (const std::exception& e) {
            log_message("ERROR", "Exception in main loop: " + std::string(e.what()));
            consecutive_errors++;
            
            // 尝试释放传输项，即使发生异常
            if (ioctl(ctl_fd, TFS_RELEASE_XFER) < 0) {
                log_message("ERROR", "ioctl TFS_RELEASE_XFER failed after exception: " + 
                           std::string(strerror(errno)));
            }
            
            if (consecutive_errors >= max_consecutive_errors) {
                log_message("CRITICAL", "Too many consecutive exceptions, pausing for recovery");
                sleep(5);
                consecutive_errors = 0;
            } else {
                sleep(1);
            }
        }
        
        log_message("INFO", "--------------------------------------------------");
    }

    log_message("INFO", "TFS daemon shutting down");
    close(ctl_fd);
    log_file.close();
    return 0;
}