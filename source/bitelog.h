/*
    日志操作封装
    1.防止头文件重复包含
    2.包含头文件
    3.声明命名空间
    4.声明全局日志器
    5.声明日志配置结构体
    6.声明全局日志器初始化接口
    7.封装日志输出宏


*/
    //1.防止头文件重复包含
#pragma once
    //2.包含头文件
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include<spdlog/sinks/rotating_file_sink.h>
#include<spdlog/async.h>
    //3.声明命名空间
namespace bitelog {
    //4.声明全局日志器
    extern std::shared_ptr<spdlog::logger> g_logger;
    //5.声明日志配置结构体
    struct Logsettings {
        bool async;//是否异步日志
        int level;//日志输出等级: trace=0, debug=1, info=2, warn=3, error=4, critical=5, off=6
        std::string pattern;//日志输出格式[%H:%M:%S] [%-7l]: %v
        std::string path;//日志文件目标 stdout, file, 
    };
    //6.声明全局日志器初始化接口
    extern void bitelog_init(const Logsettings &settings);
    //7.封装日志输出宏
    #define FMT_PREFIX "[{}:{}]:"//日志输出格式前缀，包含文件名和行号
    #define DBG(fmt, ...) bitelog::g_logger->debug(FMT_PREFIX fmt,__FILE__,__LINE__ ##__VA_ARGS__)
    #define INF(fmt, ...) bitelog::g_logger->info(FMT_PREFIX fmt,__FILE__,__LINE__ ##__VA_ARGS__)
    #define WRN(fmt, ...) bitelog::g_logger->warn(FMT_PREFIX fmt,__FILE__,__LINE__ ##__VA_ARGS__)
    #define ERR(fmt, ...) bitelog::g_logger->error(FMT_PREFIX fmt,__FILE__,__LINE__ ##__VA_ARGS__)
} // namespace bitelog
