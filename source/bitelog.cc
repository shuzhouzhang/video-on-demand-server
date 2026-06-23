#include "bitelog.h"
namespace bitelog {
    std::shared_ptr<spdlog::logger> g_logger;
    void bitelog_init(const Logsettings &settings)
    {
        // 1.判断日志器类型--async or sync
        if (settings.async) {
            if(settings.path == "stdout") {
              g_logger = spdlog::stdout_color_mt<spdlog::async_factory>("stdout_logger");
            }
            else
            {
                g_logger = spdlog::basic_logger_mt<spdlog::async_factory>("file_logger", settings.path);
            }
        }
        else {
            if(settings.path == "stdout") {
              g_logger = spdlog::stdout_color_mt("stdout_logger");
            }
            else
            {
                g_logger = spdlog::basic_logger_mt("file_logger", settings.path);
            }
        }
        //2.判断输出目标stdout, file, 
        //3.创建日志器
        //4.设置日志输出等级
        g_logger->set_level(static_cast<spdlog::level::level_enum>(settings.level));
        //5.设置日志输出格式
        g_logger->set_pattern(settings.pattern);
    }
    
   
} // namespace bitelog
