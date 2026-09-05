# video-on-demand-server

C++17 视频点播服务端，面向 Qt 客户端提供 HTTP/JSON API。项目从原来的单体 `video_server` 逐步拆分为轻量微服务形态，同时保留原有业务接口，优先保证已有客户端功能可运行。

## 项目介绍

最新拆分说明和实测边界见 [服务拆分与参考项目对照](docs/reference-service-boundaries.md)。
用户、视频与互动路由由各服务显式装配；登录、用户资料、视频列表、搜索和详情
已走强类型业务 RPC，其余接口继续使用兼容桥接。

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
brpc/protobuf        brpc/protobuf        brpc attachment
    |                    |                    |
user_service :11002  video_service :11003  file_service :11001
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
- `transcode_service`：MySQL 持久化异步转码服务。任务与 RabbitMQ outbox 事件在同一事务写入；发布器使用 confirm 重试。开启 RabbitMQ 时由消息驱动 Worker，关闭时保留数据库轮询。当前生成 MP4，尚未实现 HLS 分片。
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
gateway_service 删除客户端伪造的内部身份头
    |
Redis token -> account
    |
注入 X-Authenticated-Account + X-Gateway-Verified: 1
    |
下游绑定认证 account；兼容字段缺省时补齐、相异时返回 403
```

`Authorization` 和 `X-Request-Id` 继续转发。客户端传入的
`X-Authenticated-Account`、`X-Authenticated-Role`、
`X-Gateway-Verified` 会被 Gateway 无条件删除。

## 安全运行模式

- Redis/auth 开启时，受保护路由缺少或使用无效 Bearer token 返回 401，
  Redis 查询异常返回 503。
- 下游设置 `auth.enforce_gateway_identity=true` 后只信任 Gateway 身份；
  绕过 Gateway 且缺少内部身份返回 401，客户端 account 不一致返回 403。
- Docker 用户/视频服务默认启用严格身份绑定。`*.local.json` 中的 false
  仅用于没有 Redis 的本地演示和旧 Qt 请求兼容，不是生产安全配置。
- `X-Authenticated-Account` / `X-Gateway-Verified` 是内部信任头。下游服务
  不应直接暴露给不可信网络；生产环境应只允许 Gateway 所在网段访问下游端口。
- 管理接口在严格模式查询 `users.role/status`，只允许状态为“启用”的
  “管理员”或“超级管理员”，不读取客户端 role。
- 公开视频统一要求 `status=1 AND review_status='审核通过' AND transcode_status='READY'`。新上传的
  待审核视频仍返回创建结果，并可由本人通过 `/users/videos` 查看。

### 上传内存边界

当前 cpp-httplib 版本支持 content receiver，但 Gateway 为兼容现有 Qt
multipart 请求仍会从 `Request::files` 重建下游请求，因此端到端仍是内存型上传。
在完整流式改造前，视频/通用文件上限已降为 64 MiB，HTTP 请求体总上限为
80 MiB；服务会在解析前拒绝更大的请求。文件名使用密码学随机后缀并以排他
创建方式写入，保留原扩展名，不会覆盖同名文件。

### Docker 开发凭据

`docker-compose.yml` 中的 MySQL 密码仅为本地开发示例，不得直接用于生产。
生产部署必须通过 secret 管理设施注入独立强密码，并限制 MySQL、Redis 和
所有下游服务端口只在内部网络可达。

### 密码存储

- 新密码使用 OpenSSL PBKDF2-HMAC-SHA256，当前迭代次数为 210000，
  每次生成独立的 16 字节 CSPRNG salt。
- 数据库存储格式为
  `$pbkdf2-sha256$v=1$i=<iterations>$<salt-hex>$<digest-hex>`，便于后续升级
  参数或算法。
- 登录查询只按 account 读取密码摘要，不再在 SQL 中比较明文密码。
- 历史明文账号仅作为迁移兼容：首次正确登录后通过条件 UPDATE 自动
  换成版本化摘要；密码和完整摘要都不会写入日志。

迁移前需执行：

```bash
make migrate
./database_migrate conf/server.local.json \
  migrations/013_harden_credentials_and_email_codes.sql
```

### 邮箱验证码

- 使用 OpenSSL CSPRNG 生成 6 位数字验证码和不可预测的 authcodeId。
- 验证码有效期 10 分钟、最多失败 5 次、同一邮箱 60 秒内只允许发送一次。
- 正确验证通过带 `consumed=0`、有效期和尝试次数条件的原子 UPDATE 消费；
  只有一个并发请求能得到受影响行数 1。
- 默认响应不含 `debugCode`。只有开发者显式设置
  `VIDEO_ENABLE_EMAIL_DEBUG_CODE=1` 时才返回；生产环境不得设置该变量。

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

当前仍使用同一 MySQL schema，但数据访问已经拆成四个领域接口：

- `IUserRepository`：登录、验证码、资料和头像。
- `IVideoRepository`：公开视频、上传、详情、搜索和作者作品。
- `IInteractionRepository`：点赞、收藏、进度、评论和弹幕。
- `IAdminRepository`：管理员授权、审核和后台用户操作。

`MySqlUserRepository` 直接实现 `IUserRepository`，不再继承
`MySqlVideoRepository`。`user_service` 不链接视频 Repository 实现，
`video_service` 不链接用户 Repository 实现；旧 `video_server` 通过
`RepositorySet` 组合用户、视频/互动和管理员 Repository，继续保留原路由。

主要表：

- `users`：账号、版本化密码摘要、昵称、角色、状态、头像、资料。
- `email_login_codes`：验证码、有效期、失败次数和消费状态。
- `email_code_rate_limits`：按邮箱串行化发送频率租约。
- `videos`：视频元数据、作者、播放地址、审核状态。
- `video_likes`：点赞关系。
- `video_favorites`：收藏关系。
- `video_watch_progress`：观看进度。
- `video_comments`：评论。
- `video_barrages`：弹幕。

## 本地构建

Linux 首次运行不需要 `sudo`。下面的命令会把 MariaDB 11.4.10 和
Redis 7.2.15 安装到当前用户的 `~/.local/opt`，数据保存在
`~/.local/var`，随后创建业务库并执行 `migrations/001` 到 `015`：

```bash
make dev-infra-bootstrap
```

基础设施已经安装后，可分别管理和检查：

```bash
make dev-db-start
make dev-db-migrate
make dev-redis-start
make dev-db-status
make dev-redis-status
```

启动微服务（会确保 MariaDB 和 Redis 已启动，但首次仍需先执行上面的
bootstrap 和迁移）：

```bash
make microservices
make dev-start-ms
make dev-status-ms
make dev-smoke-ms
make dev-smoke-write-ms
make dev-stop-ms
```

需要同时停止用户态基础设施时执行 `make dev-infra-stop`。

默认端口：

```text
gateway_service  10000
file_service     10001
user_service     10002
video_service    10003
transcode_service 10004
internal brpc    11001-11004
```

## Reference runtime（单机）

CMake 是 reference runtime 的正式构建入口，Makefile 只提供快捷包装：

```bash
make cmake-configure
make cmake-build CMAKE_BUILD_JOBS=2
```

固定依赖全部安装在当前用户目录 `/home/dev/.local/opt/vod`，不需要
sudo：

- etcd 3.7.1：服务租约注册与 Gateway 实例刷新。
- Erlang/OTP 27.3 + RabbitMQ 4.3.4：持久化事件和 publisher confirm。
- Elasticsearch 9.4.2：`vod_videos_v1` 索引和 `vod_videos` 别名。
- FastDFS：一个 tracker、一个 storage，复用系统已安装客户端。

```bash
make reference-infra-bootstrap
make reference-infra-start
make reference-infra-status
make reference-infra-stop
```

启动顺序为 etcd → RabbitMQ → Elasticsearch → FastDFS → MariaDB/Redis →
业务服务 → Gateway。业务服务的 HTTP 应用层只监听 `127.0.0.1`，Gateway
通过 etcd 获取 11001–11004 的 brpc 实例。迁移期未强类型化的旧路由通过
`runtime.proto/InternalHttpService` 承载 protobuf RPC；文件上传和二进制下载
使用 brpc attachment，不把大文件放进 protobuf `bytes`。

已强类型化的路由直接进入 `UserService` / `VideoService` 并调用 Repository，
不经过本机 HTTP；响应头 `X-Vod-Rpc-Method` 可用于验证方法选择。
完整中间件版构建后，可执行 `ctest --test-dir build/reference-runtime --output-on-failure`。
不安装中间件时使用 `cmake -S server -B build/portable -DVOD_ENABLE_REFERENCE_RUNTIME=OFF`，
再执行 `cmake --build build/portable --parallel 2`；这是不同的运行配置，不能拿它冒充 MQ/RPC 实测。

基础设施端口：

```text
etcd client/peer       2379 / 2380 (127.0.0.1)
RabbitMQ AMQP          5672 (127.0.0.1)
Elasticsearch HTTP     9200 (127.0.0.1, 512 MB heap)
FastDFS tracker/storage 22122 / 23000
```

FastDFS 6.12.x 拒绝 loopback tracker 地址，因此脚本会选择容器或 VM 的第一个
非 loopback 内部地址，并将 tracker/storage 的 `bind_addr` 绑定到该地址；
`allow_hosts` 只保留 `127.0.0.1` 和这个内部地址。地址选择不正确时可显式设置
`VOD_FASTDFS_HOST` 后重新执行 bootstrap。

真实 RabbitMQ 密码只保存在
`/home/dev/.local/opt/vod/secrets/rabbitmq_password`，仓库配置仅提交
`password_file` 路径。ES 若记录 `vm.max_map_count` 警告但单节点健康检查为
green，可以用于本地闭环；生产部署仍必须由管理员调整系统参数。

常用排查：

```bash
bash tools/dev_reference_infra.sh status
tail -n 100 /home/dev/.local/opt/vod/logs/etcd.log
tail -n 100 /home/dev/.local/opt/vod/logs/elasticsearch/vod-dev.log
tail -n 100 /home/dev/.local/opt/vod/data/fastdfs/storage/logs/storaged.log
```

接口覆盖检查：

```bash
make audit-routes
```

自动测试：

```bash
make test
```

覆盖 Bearer 解析、Gateway 头清洗/身份注入、账号越权、管理员授权、
logout 纯 token 与幂等性、待审核视频边界和上传后的内部回读。

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

转码接口（均由网关校验登录身份，账号只能访问自己的任务）：

- `POST /transcode/jobs`：为已有视频补建或重新排队任务，参数为 `videoId`；服务端从数据库读取源文件路径，不接受客户端指定任意文件路径。
- `GET /transcode/jobs?videoId=...`：查询任务状态、尝试次数和最后错误。
- `POST /transcode/jobs/retry`：将已失败或已完成的任务显式重新排队。

上传到本地 `uploads` 目录的视频会自动创建 `PENDING` 任务。只有审核通过且 `transcode_status=READY` 的视频会进入公开列表和播放地址查询。Docker 镜像安装 FFmpeg，`video_service` 与 `transcode_service` 共享 `uploads_data` 卷。
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
- Redis 用户资料缓存：`user_service` 使用 cache-aside；缓存未命中读取 MySQL，
  资料或头像更新后立即失效。基础 TTL 为 3600 秒，并增加 0 到 3600 秒随机
  抖动，避免大量 key 同时过期。
- Docker 部署：`docker compose up --build` 启动服务、MySQL、Redis 和数据库迁移。

## 当前边界

- 第一阶段仍是共享 MySQL schema，未强行引入分布式事务。
- `/videos/upload` 和 `/users/avatar` 为兼容现有 Qt 客户端仍保留旧路径；`file_service` 已承接 `/uploads/...` 下载和通用 `/files/upload`。
- 未引入 etcd 注册中心、真实消息队列、服务网格或复杂熔断组件，避免超出当前项目维护能力；服务发现通过 `ServiceRegistry` 配置抽象实现，其他边界已通过 `svc_sync`、`svc_mq`、`svc_worker` 预留。
- 内部身份头依赖 Gateway 与下游的网络隔离；若下游未来跨不可信网络
  暴露，需要增加 mTLS 或内部请求签名。
