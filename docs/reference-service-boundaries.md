# 服务拆分与参考项目对照

参考对象是 [cpp-microservice-videoplayer/server](https://gitee.com/bitedu-cpp-team/cpp-microservice-videoplayer/tree/master/server)，参考提交 `94c8a0bb4cd019f769c149757ca5b31171e12cb7`。
虚拟机 `/home/dev/workspace/video-on-demand-server` 是用户自己的原项目。当前改造在独立验证目录运行，未覆盖原项目。

## 本轮完成的四项工作

| 项目 | 已实现 | 验证 |
|---|---|---|
| 剩余业务 RPC | 用户、视频、互动、审核、文件上传下载、转码任务均映射到具名 protobuf RPC；Gateway 不再通过通用桥接调用这些业务 | 实测响应头记录 35 个 RPC 方法；测试检查账号绑定、管理员权限、缺失参数与显式零值 |
| 文件 ID 全链路 | 视频源文件、封面、头像经 `FileService` 写入 FastDFS；数据库保存对象定位 ID，转码服务通过文件 RPC 下载源文件 | 五个服务分别从独立工作目录运行，用户/视频服务没有创建 uploads 目录；源文件和头像逐字节往返一致 |
| HLS | FFmpeg 生成 VOD m3u8/TS；先上传全部分片、重写清单为 `/uploads/<对象ID>`，最后发布清单并以租约条件切换播放地址 | 9 秒真实视频生成三个分片，清单含 ENDLIST；清单和全部分片通过 Gateway 下载，MIME 正确 |
| 缓存事件与失败恢复 | 用户资料/头像更新和失效事件同一 MySQL 事务；RabbitMQ 消费完成才去重落账；Redis generation/Lua 防止旧读回填；Worker 定期恢复过期租约 | 双缓存实例的确定性回填竞态、重复事件、Redis 停机恢复、文件服务停机重试、强杀 Worker 后恢复均通过 |

## 调用与代码边界

```text
Qt / HTTP 客户端
  → Gateway：JSON/multipart → 具名 protobuf 请求，Redis 校验身份
  → UserService / UserOperations / VideoService / VideoOperations /
    InteractionOperations / FileService / TranscodeOperations
  → Repository / FileService / MySQL 事务 Outbox

视频上传 → FileService → FastDFS 对象 ID → 视频表 + 转码任务 + Outbox
RabbitMQ 通知 → 唤醒 Worker；数据库队列负责任务状态、抢占、延迟重试
Worker → FileService 下载源文件 → 私有工作目录 FFmpeg → 分片对象 → 清单对象
      → 带 lease_token 条件提交播放地址
```

- `user_routes`、`video_routes`、`interaction_routes` 现在构造具名业务处理器。HTTP 注册和 RPC 调用复用处理器内的验证与 Repository 操作。
- `business.proto` 为各个请求声明领域字段，使用字段存在性区分“没传 seconds”和“传了 0”。输入没有任意 URL、HTTP 方法或 JSON 字符串字段。
- `business_adapter.h` 仍适配旧处理器的内存 `httplib::Request/Response` DTO。这是代码层面的过渡耦合；不会构造 HTTP 客户端、请求本机端口或进行路由查找。不能把它描述为已经完全消除了 HTTP 类型依赖。
- 原四个 `UserService/VideoService` 方法继续直接访问 Repository。`X-Vod-Rpc-Method` 标识实际业务方法。业务 RPC 失败不会回退到 HTTP。
- 通用桥接仍保留给兼容/开发入口，`/__smoke-cleanup` 不属于公开业务合同；正常业务覆盖不依赖它。
- 对象 ID 使用存储返回的定位符。内部以 `object:` 标识，公开访问统一走 `/uploads/`。`cover_path` 保存封面对象 ID，原 `cover_file_name` 保留展示名称。

## 失败行为

- 文件服务失败：上传返回失败；已知的部分上传对象会被回收。转码读取失败后按数据库 `next_attempt_at` 延迟重试。
- Worker 退出：任务保持 RUNNING 至租约过期，活跃 Worker 定期恢复并重新抢占；旧租约不能提交完成状态。新租约回收同一任务遗留的私有目录。
- MQ 通知重复/缺失：事件只负责唤醒，任务是否完成以数据库为准；扫描作为恢复后备，不再收到某个事件就执行任意一条任务并声称该事件的任务已完成。
- Redis 失败：MySQL 更新与 Outbox 仍能提交；消费端把依赖故障标为可重试，延迟重试直到恢复。无效消息仍有重试上限和死信队列。
- 并发读写缓存：读取数据库前取得 generation，回填时 Lua 比较 generation；失效事件原子更换随机 generation 并删除资料。因此失效之前读出的旧快照不能在失效之后重新填入缓存。
- Outbox 发布仍有有限重试预算，耗尽的 FAILED 事件需要运维重放；这里没有声称无限时长故障下无需人工处理。

## 验证与复现（2026-09-05）

- `make test`：184 条 PASS，无 FAIL；转码与 Outbox 测试正常退出。
- CMake `VOD_ENABLE_REFERENCE_RUNTIME=OFF`：五个服务构建通过。
- VM CMake `VOD_ENABLE_REFERENCE_RUNTIME=ON`：五个服务构建通过。
- `native_rpc_contract`：17 个断言通过，包含身份/权限、字符串 ID、字段存在性、远程上传失败回收。
- Redis/MySQL/Gateway 既有集成回归：30 条 PASS，包含并发点赞、任务租约与鉴权错误区分。
- `profile-cache`：7 个断言通过，以真实 Redis 和可控制的数据库读时序验证回填竞态，并覆盖 Redis 丢失 generation 后的旧读。
- `reference_runtime_test.py`：35 个 RPC 方法实测，上传 → FastDFS → HLS → 播放清单/分片、互动、审核、显式重试均通过。
- 事务故障注入：用隔离数据库触发器拒绝 Outbox INSERT，确认资料 UPDATE 随之回滚。
- 故障注入：停止测试文件服务后恢复；对测试 Worker 进程组发 SIGKILL 后恢复；停止测试 Redis 超过普通消息重试次数后恢复，均通过。

本次专用环境：`/home/dev/workspace/vod-boundaries-01a07149`，数据库 `vod_boundaries_01a07149`、Redis `16379`、RabbitMQ vhost `/vod-boundaries-01a07149`、etcd 前缀 `/vod/review/01a07149`、ES 别名 `vod_review_01a07149`，HTTP `12000–12004`、RPC `13001–13004`。

仅在已配好的隔离 fixture 中运行故障测试。fixture 需要 `.vod-test-fixture` 标记和 `review-config` 下六份配置，配置必须指向上述专用库、端口和命名空间：

```bash
cmake -S server -B build -DVOD_ENABLE_REFERENCE_RUNTIME=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
python3 test/integration/reference_fixture_test.py --fixture-root "$PWD"
```

脚本只停止自己启动的进程。测试业务进程和测试 Redis 已停止；测试对象、数据库记录及日志保留用于复核。
证据文件：`remaining-runtime-results.log`、`remaining-evidence.json`、`remaining-ctest.log`；不包含会话 Token。

## 与参考项目的合理区别

这一轮已补齐前一轮列出的四项工作，服务划分和关键运行链路已达到可对照的程度，但不意味着功能、代码结构或生产能力完全等价。
仍共享 MySQL schema；未验证跨主机部署、Qt GUI 播放或生产负载。原 portable/Docker 兼容配置仍可使用本地目录和 MP4；上述 HLS/远程对象链路对应开启 registry 和 reference runtime 的配置。
文件仍有 64 MiB 上限，原生 RPC 使用 bytes，未做流式上传。失去响应的上传可能留下无法立即定位的孤立对象，旧版本转码产物也尚无生命周期清理器。

理解检查：如果 RabbitMQ 的唤醒消息重复了两次，为什么不会有两个 Worker 同时成功提交同一任务？答案应落到数据库抢占和 lease_token 条件，而不是仅回答“消费者做了去重”。
