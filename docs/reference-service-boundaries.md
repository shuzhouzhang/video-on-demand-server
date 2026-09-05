# 服务拆分与参考项目对照

本次对齐对象是 [cpp-microservice-videoplayer 的 server](https://gitee.com/bitedu-cpp-team/cpp-microservice-videoplayer/tree/master/server)，
固定参考提交 `94c8a0bb4cd019f769c149757ca5b31171e12cb7`。虚拟机中的
`/home/dev/workspace/video-on-demand-server` 是本项目，不是参考仓库。

## 整合基线

- 本地 `51122a1`：身份绑定、密码学随机 Token、Redis RAII/互斥保护、数据库连接池与预处理 SQL、上传排他创建等修复。
- 原虚拟机与远端 `4f636ed`：brpc 兼容桥接、etcd 发现、RabbitMQ/Outbox、Redis 用户缓存、ES 搜索、FastDFS 文件适配。
- 两者是分叉分支。本次合并其能力，再实现下述服务边界与业务 RPC。

## 本次新增的边界

1. `common/http_server.cc` 只负责 HTTP 监听、请求体限制、健康检查，接收路由注册函数。
2. 用户路由放在 `svc_user/source/user_routes.cc`；视频与互动路由放在
   `svc_video/source/{video_routes,interaction_routes}.cc`。
3. 服务二进制显式链接自己的路由，不链接 `compatibility_http_server.cc`。
   即使错误地给用户路由传入完整 RepositorySet，也不会注册视频接口。
4. 单体通过 `compatibility_http_server.cc` 组合旧入口；独立用户/视频服务不再挂载 `/uploads`。
5. 六个客户端入口已经映射到四个强类型业务 RPC：

| 客户端请求 | 实际内部方法 |
|---|---|
| `POST /login`、`POST /login/password` | `UserService.Login` |
| `GET /users/profile` | `UserService.GetProfile` |
| `GET /videos`、`GET /videos/search` | `VideoService.ListVideos` |
| `GET /videos/detail` | `VideoService.GetVideoDetail` |

这些方法直接调用 Repository，不经过本机 HTTP 回环。Gateway 保持原 JSON
字段和 HTTP 状态语义，响应的 `X-Vod-Rpc-Method` 用于确认实际调用路径。
已经迁移的方法失败后不回退到 HTTP，避免隐藏版本不匹配或重复执行。
protobuf 增加字符串 `video_id`，保留原数字字段编号，不把 `video-xxx` 截断成数字。

## 与参考实现相比

| 维度 | 当前状态与限制 |
|---|---|
| 服务职责和进程 | 同样按 Gateway、用户、视频、文件、转码划分，独立构建和运行 |
| 服务发现 | 整合已有 etcd 租约注册与 Gateway 刷新，不是本次从零新增 |
| 业务 RPC | 核心查询和登录已强类型化；上传、互动、审核等其余路由仍使用 `InternalHttpService` 桥接 |
| 异步任务 | 整合已有 RabbitMQ 和数据库 Outbox；实测真实视频转码成功，不代表所有故障场景已覆盖 |
| 文件存储 | 通用文件上传可用 FastDFS；兼容视频上传、头像和转码仍依赖共享目录，尚未实现所有媒体都通过文件 ID 流转 |
| 搜索与缓存 | 整合 ES 搜索与 Redis 用户资料缓存；缓存同步占位模块不应描述为完整 MQ 缓存一致性方案 |
| 转码产物 | 当前仍为 H.264/AAC MP4，未对齐参考的 HLS m3u8/TS 分片；历史事件名称带 HLS 不代表实现了 HLS |
| 数据边界 | 仍共享 MySQL schema，管理员授权等仍查询用户表，未实现独立数据库所有权 |

因此，这次交付是可运行的进一步拆分和参考运行栈整合，**不应宣称已经与参考项目完全等价**。
后续完整对齐顺序：剩余业务强类型 RPC → 文件 ID/远程对象全链路 → HLS → 缓存事件与失败恢复验证。
不要为了增加服务数量，先拆出没有清晰职责或没有调用者的空服务。

## 验证记录（2026-09-05）

- WSL Ubuntu 22.04：`make test` 成功，输出 184 条 PASS，无 FAIL；转码和 Outbox 测试亦正常退出。
- WSL：CMake `VOD_ENABLE_REFERENCE_RUNTIME=OFF` 五个服务构建成功。
- Linux VM 独立目录 `/home/dev/workspace/vod-boundaries-01a07149`：开启 reference runtime 的五个服务构建成功。
- `ctest --test-dir build --output-on-failure`：`native_rpc_contract` 通过，覆盖资料身份绑定、字符串 ID、缺失 ID、错误密码及请求 ID。
- VM：独立 MySQL 库 `vod_boundaries_01a07149`、Redis `16379`、RabbitMQ vhost
  `/vod-boundaries-01a07149`、etcd 前缀 `/vod/review/01a07149`、ES 别名 `vod_review_01a07149`。
- VM：HTTP `12000–12004`、brpc `13001–13004`，运行真实 MP4 上传、RabbitMQ 转码、头像访问及清理 smoke，通过。
- VM：实际响应头确认四个原生 RPC 方法；进程级外来路由与直接文件访问返回 404。
- VM：`make integration-redis integration-mysql integration-gateway-auth` 成功，30 条 PASS；覆盖 Redis 50 线程隔离/断线重连、并发点赞、唯一视频 ID、转码租约与重试上限、网关 401/503。测试配置首次误用了 RabbitMQ 密码文件，改为与服务相同的数据库密码字段后通过。
- VM：测试完成后停止测试启动的业务进程与测试 Redis；原项目工作区保持原分支和干净状态。
- 以上不是 Qt GUI 端到端、跨主机部署或生产负载测试。独立测试库、vhost、ES 索引和日志保留用于复核。

## 理解这次改动

以前的路径是 `Gateway → brpc 通用转发 → 本机 HTTP → Repository`。
核心接口现在是 `Gateway → UserService/VideoService 业务方法 → Repository`。
服务边界既体现在运行时通信，也体现在二进制只链接本服务路由。
检查理解时可以追问：用户服务传入了视频 Repository，为什么仍不能响应 `/videos`？
