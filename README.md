# video-on-demand-server

C++ 视频点播服务端，面向 Qt 客户端提供 HTTP/JSON API。这个仓库用于“视频网站客户端与 C++ 后端接口联调项目”的演示和实习简历，不是复杂微服务架构，也不是生产级登录鉴权系统。

当前后端覆盖客户端主要接口：视频列表、详情、搜索、播放地址、点赞、收藏、观看进度、评论、弹幕、用户资料、头像上传、视频发布、视频文件上传、公开视频审核过滤，以及后台审核/用户管理的 demo 管理接口。

## 项目模块

- `source/http_server.*`：HTTP 服务、路由注册、JSON 响应、上传处理和 `/uploads/...` 静态资源访问。
- `source/video_repository.*`：视频、用户、互动、评论、弹幕、审核等业务数据访问接口和 MySQL 实现。
- `source/database.*`：MySQL/ODB 连接、转义、查询和执行封装。
- `source/config.*`：服务端、日志、数据库配置读取。
- `source/bitelog.*`：服务端日志初始化与输出封装。
- `source/util.*`：JSON 转换、文件读写、字符串处理和随机值生成。
- `migrations/`：数据库表结构、字段、索引和种子数据迁移。
- `test/`：当前单元测试和 HTTP 端到端测试入口。
- `tools/`：接口覆盖审计和真实服务冒烟测试脚本。

## 依赖

推荐在 Linux 环境中构建运行。需要：

- C++17 编译器：`g++`
- 构建工具：`make`，可选 `cmake`
- MySQL 客户端开发库：`mysqlclient`
- ODB MySQL 运行库：`libodb`、`libodb-mysql`
- JSON/日志/格式化库：`jsoncpp`、`fmt`、`spdlog`
- HTTP 头文件库：`cpp-httplib`，需要编译器能找到 `<httplib.h>`
- Python 3：运行 `tools/audit_routes.py` 和 `tools/smoke_api.py`
- curl/procps：`make dev-status`、`make dev-start` 使用 `curl`、`pgrep`、`pkill`

Ubuntu/Debian 可参考：

```bash
sudo apt update
sudo apt install -y g++ make cmake python3 curl libjsoncpp-dev libfmt-dev libspdlog-dev libcpp-httplib-dev default-libmysqlclient-dev libodb-dev libodb-mysql-dev
```

`cpp-httplib`、`libodb`、`libodb-mysql` 的包名随发行版不同可能不一致；如果 `make server` 报 `<httplib.h>`、`-lcpp-httplib` 或 `-lodb-mysql` 找不到，需要先安装对应开发包，或把头文件/库路径加入编译参数。

## 本地配置

复制配置模板：

```bash
cp conf/server.json conf/server.local.json
```

然后在 `conf/server.local.json` 中填写真实 MySQL 连接信息。这个文件已被 Git 忽略，不要提交真实密码。

可选环境变量：

- `VIDEO_ADMIN_TOKEN`：设置后，`POST /admin/reviews/action` 和 `POST /admin/users/action` 必须携带 `X-Admin-Token` 请求头，或在 JSON 中传 `adminToken`。
- `VIDEO_DEBUG_EMAIL_CODE=1`：本地调试邮箱验证码时，`POST /login/email-code` 会额外返回 `debugCode`。默认不会返回验证码。
- `VIDEO_ENABLE_SMOKE_CLEANUP=1`：仅 `make dev-start` 使用，开启 `POST /__smoke-cleanup` 供写入 smoke 测试清理数据。

## 数据库迁移

先编译迁移工具：

```bash
make migrate
```

按顺序执行迁移：

```bash
./database_migrate conf/server.local.json migrations/001_create_videos.sql
./database_migrate conf/server.local.json migrations/002_create_video_likes.sql
./database_migrate conf/server.local.json migrations/003_create_watch_progress.sql
./database_migrate conf/server.local.json migrations/004_create_video_favorites.sql
./database_migrate conf/server.local.json migrations/005_add_video_play_url.sql
./database_migrate conf/server.local.json migrations/006_create_video_comments.sql
./database_migrate conf/server.local.json migrations/007_create_video_barrages.sql
./database_migrate conf/server.local.json migrations/008_create_users.sql
./database_migrate conf/server.local.json migrations/009_add_video_owner_account.sql
./database_migrate conf/server.local.json migrations/010_add_login_fields.sql
./database_migrate conf/server.local.json migrations/011_add_admin_fields.sql
./database_migrate conf/server.local.json migrations/012_add_video_metadata_upload_fields.sql
./database_migrate conf/server.local.json migrations/013_add_public_video_review_indexes.sql
./database_migrate conf/server.local.json migrations/014_add_email_code_expiry.sql
```

新增迁移都是幂等写法，重复执行时会检查字段或索引是否存在。

## 构建与启动

编译服务端：

```bash
make server
```

手动前台启动：

```bash
./video_server conf/server.local.json
```

开发演示推荐使用一键启动，它会编译 `video_server`，后台启动服务，并把日志写到 `/tmp/video_server_dev.log`：

```bash
make dev-start
make dev-status
make dev-smoke
```

停止开发服务：

```bash
make dev-stop
```

如果客户端从 Windows 访问虚拟机，确认虚拟机 IP 后访问例如 `http://192.168.19.129:9000`。宿主机可以这样验证：

```bash
python3 tools/smoke_api.py --base-url http://192.168.19.129:9000
```

## 审计与测试

检查后端是否覆盖客户端需要的路由：

```bash
make audit-routes
```

预期关键输出：

```text
MISSING_GET: []
MISSING_POST: []
```

检查 Python 脚本语法：

```bash
python3 -m py_compile tools/audit_routes.py tools/smoke_api.py
```

运行当前自动测试入口：

```bash
make test
```

如果因为系统依赖缺失导致失败，请优先补齐 `jsoncpp/fmt/spdlog/httplib/mysqlclient/ODB` 等依赖，不要把缺依赖误判成业务代码失败。

## 真实服务冒烟测试

只读 smoke 测试：

```bash
make smoke BASE_URL=http://127.0.0.1:9000
```

验证写入链路，包括视频上传、审核通过、播放地址读取、头像上传和 `/uploads/...` 静态资源访问：

```bash
make dev-smoke-write
python3 tools/smoke_api.py --base-url http://192.168.19.129:9000 --write-checks
```

如果设置了 `VIDEO_ADMIN_TOKEN`，运行写入 smoke 测试时也需要在同一个 shell 中设置相同环境变量，脚本会自动带上 `X-Admin-Token`。

## 常用接口示例

```bash
curl http://127.0.0.1:9000/health
curl http://127.0.0.1:9000/videos
curl 'http://127.0.0.1:9000/videos/detail?id=video-002'
curl 'http://127.0.0.1:9000/videos/search?keyword=%E7%BC%96%E7%A8%8B'
curl 'http://127.0.0.1:9000/videos/play-url?videoId=video-002'
curl 'http://127.0.0.1:9000/users/profile?account=bit-user-001'
curl 'http://127.0.0.1:9000/admin/reviews'
```

发布视频元数据：

```bash
curl -X POST http://127.0.0.1:9000/videos \
  -H 'Content-Type: application/json' \
  -d '{"title":"新发布视频","account":"bit-user-001","userName":"BIT 用户","category":"科技","tags":["后端"],"description":"元数据发布","videoFileName":"new-video.mp4"}'
```

上传视频文件：

```bash
cat > /tmp/video-metadata.json <<'JSON'
{"title":"上传视频","account":"bit-user-001","userName":"BIT 用户","category":"科技","tags":["上传"],"description":"multipart 文件上传","videoFileName":"sample.mp4"}
JSON

curl -X POST http://127.0.0.1:9000/videos/upload \
  -F 'metadata=@/tmp/video-metadata.json;type=application/json' \
  -F videoFile=@sample.mp4
```

新上传视频默认 `review_status='待审核'`，公开视频列表、详情、搜索、播放地址、评论、弹幕、点赞、收藏、观看进度等公共接口只允许访问 `status=1 AND review_status='审核通过'` 的视频。用户自己的投稿列表 `/users/videos?account=...` 可以看到自己的待审核投稿。

审核通过示例：

```bash
export VIDEO_ADMIN_TOKEN=dev-admin-token

curl -X POST http://127.0.0.1:9000/admin/reviews/action \
  -H 'Content-Type: application/json' \
  -H "X-Admin-Token: $VIDEO_ADMIN_TOKEN" \
  -d '{"videoId":"video-003","status":"审核通过"}'
```

## 安全边界与后续优化

当前项目是 demo/联调模式：

- 普通用户接口仍主要依赖请求体或 query 中的 `account` 字段识别用户，没有完整 session/cookie/JWT 登录态。
- 管理员写接口支持轻量 `VIDEO_ADMIN_TOKEN` 保护，适合本地演示，不适合公网生产环境。
- SQL 当前通过 `mysql_real_escape_string` 做输入转义，后续应逐步迁移到 prepared statement。
- 密码仍是演示用明文种子数据，后续应改为哈希存储。
- 邮箱验证码目前只保存到数据库，不接真实邮件服务；默认不返回验证码，调试时可用 `VIDEO_DEBUG_EMAIL_CODE=1`。
- 上传文件限制了后缀和大小，并使用随机文件名保存；后续可增加 MIME 嗅探、视频转码、对象存储和异步审核。

## 面试可讲点

- 一次请求流程：`httplib` 接收 HTTP 请求，`HttpServer::registerRoutes()` 中的路由解析参数和 JSON，调用 `VideoStore` 接口，MySQL 仓储执行查询/写入，最后统一序列化为 JSON 响应。
- 视频审核流：上传或发布后默认进入 `待审核`，后台审核改为 `审核通过` 后，公共列表、详情、搜索、播放和互动接口才可访问该视频。
- 上传文件处理：multipart 解析元数据和文件，校验后缀/大小，使用账号、时间和随机串生成存储名，写入失败或数据库失败时清理孤立文件，通过受限 `/uploads/...` 路由提供静态访问。
- 观看进度写入：客户端上报 `videoId/account/seconds`，后端使用 `INSERT ... ON DUPLICATE KEY UPDATE` 保存或更新进度，读取时只允许公开视频。
- 冒烟测试和接口覆盖审计：`tools/audit_routes.py` 静态检查客户端约定路由是否注册；`tools/smoke_api.py` 对真实服务做健康检查、登录、视频查询、评论弹幕、审核和上传链路验证。
- 当前限制与优化方向：补完整登录态、prepared statement、密码哈希、真实邮件服务、文件 MIME 检测、视频转码、权限模型和生产部署方案。
