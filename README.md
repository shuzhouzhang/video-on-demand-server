# video-on-demand-server

C++17 视频点播服务端，面向 Qt 客户端提供 HTTP/JSON API。项目从原来的单体 `video_server` 逐步拆分为轻量微服务形态，同时保留原有业务接口，优先保证已有客户端功能可运行。

## 项目介绍

当前已覆盖：

- 用户注册/登录、邮箱验证码登录、退出登录
- 用户资料、头像上传、后台用户管理
- 视频列表、详情、搜索、播放地址
- 视频发布、视频文件上传、封面上传
- 点赞、收藏、评论、弹幕、观看进度
- 视频审核
- `/uploads/...` 文件访问

## 架构图

```text
Qt Client
    |
HTTP/JSON
    |
gateway_service :10000
    |
    +--------------------+--------------------+--------------------+
    |                    |                    |
user_service :10002  video_service :10003  file_service :10001
    |                    |                    |
UserRepository       VideoRepository       FileRepository
    |                    |                    |
MySQL                MySQL/shared schema    shared uploads volume
    |
RedisSessionManager
    |
Redis
```

保留兼容入口：

```text
video_server
    |
原单体 HttpServer + MySQL
```

## 服务说明

- `gateway_service`：对外统一 HTTP 入口，负责路由转发、统一错误响应、请求 ID、Redis token 校验。
- `user_service`：用户登录、邮箱验证码登录、退出登录、用户资料、头像资料更新、后台用户管理。
- `video_service`：视频元数据、列表、详情、搜索、播放地址、点赞、收藏、评论、弹幕、观看进度、审核。
- `file_service`：文件服务入口，提供 `/uploads/...` 下载和 `POST /files/upload` 通用文件上传；现有 `/videos/upload`、`/users/avatar` 为兼容 Qt 客户端仍保留原路径。
- `transcode_service`：对齐参考项目新增的转码服务入口，提供健康检查、`/transcode/jobs` 任务接收和本地 `SvcWorker` 执行队列边界，后续可替换为 HLS/FFmpeg/MQ 实现。
- `common`：JSON、日志、配置、HTTP Client、ServiceRegistry、RedisSessionManager、CacheSync 同步接口等公共能力。
- `data`：用户、视频、互动、审核、文件等跨服务领域模型。
- `database`：MySQL 连接和迁移工具。

## 请求流程

登录流程：

```text
POST /login
    |
gateway_service
    |
user_service 校验账号密码
    |
RedisSessionManager 生成 token 并写入 Redis
    |
返回 account/userName/token
```

登录后请求：

```text
Qt Client
    |
Authorization: Bearer <token>
    |
gateway_service 校验 Redis token
    |
转发到 user_service/video_service/file_service
```

文件访问：

```text
GET /uploads/...
    |
gateway_service
    |
file_service
    |
uploads 共享目录
```

## 数据库设计

当前第一阶段仍使用同一 MySQL schema，代码层已经按服务拆出 Repository 边界，后续可以继续演进为物理拆库。

主要表：

- `users`：账号、密码、昵称、角色、状态、头像、资料。
- `email_login_codes`：邮箱验证码会话。
- `videos`：视频元数据、作者、播放地址、审核状态。
- `video_likes`：点赞关系。
- `video_favorites`：收藏关系。
- `video_watch_progress`：观看进度。
- `video_comments`：评论。
- `video_barrages`：弹幕。

## 本地构建

```bash
make microservices
make dev-start-ms
make dev-status-ms
make dev-smoke-ms
make dev-smoke-write-ms
make dev-stop-ms
```

默认端口：

```text
gateway_service  10000
file_service     10001
user_service     10002
video_service    10003
transcode_service 10004
```

接口覆盖检查：

```bash
make audit-routes
```

## Docker Compose 启动

```bash
docker compose up --build
```

启动组件：

- `gateway_service`
- `user_service`
- `video_service`
- `file_service`
- `transcode_service`
- `mysql`
- `redis`
- `migrate`

访问：

```bash
curl http://127.0.0.1:10000/health
curl http://127.0.0.1:10000/videos
```

## 技术亮点

- C++17 轻量微服务拆分：先保留业务兼容，再逐步拆目录、入口、Repository 和公共库。
- Gateway 设计：统一入口、路由转发、轻量服务发现、错误响应、请求 ID、Redis token 校验。
- HTTP 内部调用：第一阶段不引入复杂 RPC，通过 `common/HttpClient` 封装下游调用。
- Repository 模式：服务入口已经按用户、视频、文件建立独立 Repository 边界。
- 公共 data 层：将视频、用户、互动、审核、文件 DTO 从服务实现中拆出，降低服务间模型耦合。
- svc_sync 边界：对齐参考项目的缓存删除/缓存回写结构，当前提供本地可运行实现，后续可接入 MQ/Redis 延迟同步。
- Redis 会话管理：登录成功生成 token，Redis 保存会话，gateway 校验登录状态。
- Docker 部署：`docker compose up --build` 启动服务、MySQL、Redis 和数据库迁移。

## 当前边界

- 第一阶段仍是共享 MySQL schema，未强行引入分布式事务。
- `/videos/upload` 和 `/users/avatar` 为兼容现有 Qt 客户端仍保留旧路径；`file_service` 已承接 `/uploads/...` 下载和通用 `/files/upload`。
- 未引入 etcd 注册中心、真实消息队列、服务网格或复杂熔断组件，避免超出当前项目维护能力；服务发现通过 `ServiceRegistry` 配置抽象实现，其他边界已通过 `svc_sync`、`svc_mq`、`svc_worker` 预留。
