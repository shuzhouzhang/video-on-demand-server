# video-on-demand-server

C++ 视频点播服务端。项目目前完成了日志模块和通用工具模块，后续将在这个基线上逐步加入配置、数据库、HTTP API 与视频业务功能。

## 当前模块

- `source/bitelog.*`：服务端日志初始化与输出封装。
- `source/util.*`：JSON 转换、文件读写、字符串切分和随机值生成。
- `source/config.*`：读取并校验服务端与日志 JSON 配置。
- `source/http_server.*`：HTTP 服务与公共路由。
- `source/database.*`：MySQL 连接与健康检查。
- `source/video*`：视频模型、MySQL查询和客户端JSON转换。
- `migrations/`：数据库表结构与可重复执行的种子数据。
- `test/util/`：通用工具的自动化测试。
- `test/config/`：配置读取与错误输入测试。
- `test/http/`：HTTP 健康检查的端到端测试。
- `test/database/`：数据库未连接和连接失败测试。

## 验证

在安装 `jsoncpp`、`fmt` 和 `spdlog` 开发库的 Linux 环境中运行：

```bash
make clean
make test
make clean
```

启动服务并检查健康状态：

```bash
make server
make migrate
cp conf/server.json conf/server.local.json
# 在 server.local.json 中填写本地数据库连接信息。
./database_migrate conf/server.local.json migrations/001_create_videos.sql
./database_migrate conf/server.local.json migrations/002_create_video_likes.sql
./database_migrate conf/server.local.json migrations/003_create_watch_progress.sql
./database_migrate conf/server.local.json migrations/004_create_video_favorites.sql
./database_migrate conf/server.local.json migrations/005_add_video_play_url.sql
./video_server conf/server.local.json
curl http://127.0.0.1:9000/health
curl http://127.0.0.1:9000/videos
curl 'http://127.0.0.1:9000/videos/detail?id=video-001'
curl 'http://127.0.0.1:9000/videos/search?keyword=%E7%BC%96%E7%A8%8B'
curl 'http://127.0.0.1:9000/videos/play-url?videoId=video-001'
curl 'http://127.0.0.1:9000/videos/like-status?videoId=video-001&account=bit-user-001'
curl 'http://127.0.0.1:9000/videos/watch-progress?videoId=video-001&account=bit-user-001'
curl 'http://127.0.0.1:9000/videos/favorite-status?videoId=video-001&account=bit-user-001'
```

`conf/server.local.json`包含本地凭据并已被 Git 忽略，不要将真实密码写入已跟踪的配置文件。
