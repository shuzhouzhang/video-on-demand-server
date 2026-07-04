# video-on-demand-server

C++ 视频点播服务端，面向 Qt 客户端提供 HTTP/JSON API。当前后端已经完成客户端 mock 合同中的主要接口覆盖，包括视频列表、详情、搜索、播放地址、点赞、收藏、观看进度、评论、弹幕、用户资料、头像上传、视频发布、视频文件上传以及后台审核/用户管理。

## 当前模块

- `source/http_server.*`：HTTP 服务、路由注册、JSON 响应、multipart 文件上传和 `/uploads` 静态文件访问。
- `source/video_repository.*`：视频、用户、互动、评论、弹幕、审核等业务数据访问接口和 MySQL 实现。
- `source/database.*`：MySQL 连接、转义、查询和执行封装。
- `source/config.*`：服务端、日志、数据库配置读取。
- `source/bitelog.*`：服务端日志初始化与输出封装。
- `source/util.*`：JSON 转换、文件读写、字符串处理和随机值生成。
- `migrations/`：数据库表结构和可重复执行的种子数据。
- `test/`：单元测试和 HTTP 端到端测试。
- `tools/`：接口覆盖审计和真实服务冒烟测试脚本。

## 构建与测试

在安装 `jsoncpp`、`fmt`、`spdlog`、`mysqlclient` 等开发库的 Linux 环境中运行：

```bash
make clean
make test
```

检查后端是否覆盖当前客户端需要的路由：

```bash
make audit-routes
```

预期关键输出：

```text
MISSING_GET: []
MISSING_POST: []
```

## 初始化数据库

先复制本地配置，并在 `conf/server.local.json` 中填写真实数据库连接信息：

```bash
cp conf/server.json conf/server.local.json
```

编译迁移工具并执行所有迁移：

```bash
make migrate
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
```

`conf/server.local.json` 包含本地凭据，已经被 Git 忽略，不要提交真实密码。

## 启动服务

开发演示推荐用一键启动命令。它会先编译 `video_server`，再用
`conf/server.local.json` 后台启动服务，并把日志写到
`/tmp/video_server_dev.log`：

```bash
make dev-start
make dev-status
make dev-smoke
```

停止开发服务：

```bash
make dev-stop
```

如果客户端从 Windows 访问虚拟机，确认虚拟机 IP 后访问
`http://192.168.19.129:9000`。在虚拟机内验证可继续用默认
`http://127.0.0.1:9000`；从宿主机验证则指定：

```bash
python tools/smoke_api.py --base-url http://192.168.19.129:9000
```

`make dev-start` 会额外启用一个只供 smoke 清理数据的内部接口
`POST /__smoke-cleanup`。普通 `./video_server conf/server.local.json`
启动不会暴露这个接口。

手动前台启动仍然可用：

```bash
make server
./video_server conf/server.local.json
```

如果配置中的端口是 `9000`，健康检查为：

```bash
curl http://127.0.0.1:9000/health
```

## 轻量微服务化运行方式

本项目保留原有 `video_server` 单体版本，同时新增一套本地轻量微服务化运行方式，用于练习业务边界拆分和 Qt 客户端兼容接入。它不是生产级微服务平台，没有引入 Docker、注册中心、网关集群、熔断限流或分布式事务。

```text
Windows Qt Client
        |
        v
API Gateway :9000
        |
        |-- user_service :9101
        |-- video_service :9102
        |-- interaction_service :9103
```

拆分边界：

- `api_gateway`：对外统一入口，保持客户端原有 HTTP 路径不变，根据路径转发到下游服务；请求没有 `X-Request-Id` 时会生成一个简单 request id，并对下游调用设置超时。
- `user_service`：处理登录、邮箱验证码登录、用户资料、头像上传和后台用户操作。
- `video_service`：处理视频列表、详情、搜索、播放地址、发布/上传、审核和 `/uploads/...` 静态资源访问。
- `interaction_service`：处理点赞、收藏、收藏列表、观看进度、评论和弹幕。

服务之间暂时使用 HTTP/JSON 通信，服务地址由 `conf/services.local.json` 静态配置：

```json
{
  "user_service": "http://127.0.0.1:9101",
  "video_service": "http://127.0.0.1:9102",
  "interaction_service": "http://127.0.0.1:9103",
  "timeout_ms": 3000
}
```

构建并启动本地微服务 demo：

```bash
make microservices
make dev-start-ms
make dev-status-ms
make dev-smoke-ms
make dev-smoke-write-ms
make dev-stop-ms
```

每个进程都提供 `GET /healthz`，例如：

```bash
curl http://127.0.0.1:9000/healthz
curl http://127.0.0.1:9101/healthz
curl http://127.0.0.1:9102/healthz
curl http://127.0.0.1:9103/healthz
```

第一版 `api_gateway` 的 `/healthz` 只返回网关自身状态，后续可以扩展为下游健康检查聚合。上传资源仍共享本地 `uploads` 目录：用户头像由 `user_service` 写入，视频文件由 `video_service` 写入，`/uploads/...` 统一经 Gateway 转发到 `video_service` 读取。生产环境应改为对象存储或独立资源服务。

当前数据库仍是同一个 MySQL 实例，代码层按用户、视频、互动边界注册路由，但没有物理拆库。后续可以按服务拆库，并通过内部接口避免跨服务直接查表。

当前不足和后续方向：

- 未接入注册中心，服务地址仍是静态配置。
- 未拆分数据库实例，也未处理跨服务分布式事务。
- 未引入服务熔断、限流、重试退避和网关鉴权。
- 上传资源暂时共享本地目录。
- 后续可接入 session/JWT、prepared statement、对象存储和统一日志链路。

## 真实服务冒烟测试

服务启动后，可以用脚本做一轮快速验收：

```bash
make smoke BASE_URL=http://127.0.0.1:9000
```

如果服务跑在虚拟机或远程开发机上，例如：

```bash
make smoke BASE_URL=http://192.168.19.129:9000
```

这个脚本会验证健康检查、登录、视频列表、详情、播放地址、用户资料、评论、弹幕和后台审核接口是否可用。

如果要验证真实写入链路，包括视频上传、头像上传和 `/uploads/...`
静态资源访问，先用 `make dev-start` 启动服务，再运行：

```bash
make dev-smoke-write
python tools/smoke_api.py --base-url http://192.168.19.129:9000 --write-checks
```

`--write-checks` 会创建一条测试视频和一个测试头像，确认静态资源能访问，
最后清理自己创建的数据库记录和上传文件。

## 常用接口示例

```bash
curl http://127.0.0.1:9000/videos
curl 'http://127.0.0.1:9000/videos/detail?id=video-001'
curl 'http://127.0.0.1:9000/videos/search?keyword=%E7%BC%96%E7%A8%8B'
curl 'http://127.0.0.1:9000/videos/play-url?videoId=video-001'
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

上传成功后，接口会返回类似 `/uploads/videos/...mp4` 的 `playUrl`，客户端可以直接通过 HTTP 访问该地址。
