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

```bash
make server
./video_server conf/server.local.json
```

如果配置中的端口是 `9000`，健康检查为：

```bash
curl http://127.0.0.1:9000/health
```

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
