# video-on-demand-server

C++ 视频点播服务端。项目目前完成了日志模块和通用工具模块，后续将在这个基线上逐步加入配置、数据库、HTTP API 与视频业务功能。

## 当前模块

- `source/bitelog.*`：服务端日志初始化与输出封装。
- `source/util.*`：JSON 转换、文件读写、字符串切分和随机值生成。
- `test/util/`：通用工具的自动化测试。

## 验证

在安装 `jsoncpp`、`fmt` 和 `spdlog` 开发库的 Linux 环境中运行：

```bash
make clean
make test
make clean
```
