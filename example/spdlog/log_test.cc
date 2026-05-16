#include <bitelog.h>
#include <gflags/gflags.h>
//1.通过gflags定义要捕获的参数
DEFINE_bool(async, true, "是否异步日志");
DEFINE_int32(level, 2, "日志输出等级: trace=0, debug=1, info=2, warn=3, error=4, critical=5, off=6");
DEFINE_string(format, "[%H:%M:%S] [%-7l]%v", "日志输出格式");
DEFINE_string(path, "file.dat", "日志文件目标 stdout, file, ");

int main(int argc, char* argv[]) {
    //2.解析命令行参数
    gflags::ParseCommandLineFlags(&argc, &argv, true);
//3.根据参数初始化日志器
   bitelog::Logsettings setings={
    .async=FLAGS_async,
    .level=FLAGS_level,
    .pattern=FLAGS_format,
    .path=FLAGS_path
   };
   bitelog::bitelog_init(setings);  
//4.输出日志
DBG("this is a debug log");
INF("this is a info log");
WRN("this is a warn log");
}