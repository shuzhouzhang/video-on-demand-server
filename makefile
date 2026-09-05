.PHONY: server migrate microservices transcode_service test integration-test integration-redis integration-mysql integration-gateway-auth redis-tsan audit-routes smoke dev-start dev-stop dev-status dev-smoke dev-smoke-write dev-start-ms dev-stop-ms dev-status-ms dev-smoke-ms dev-smoke-write-ms clean cmake-configure cmake-build reference-infra-bootstrap reference-infra-start reference-infra-stop reference-infra-status dev-db-bootstrap dev-db-start dev-db-stop dev-db-status dev-db-migrate dev-redis-bootstrap dev-redis-start dev-redis-stop dev-redis-status dev-ffmpeg-bootstrap dev-ffmpeg-status dev-infra-bootstrap dev-infra-stop

DEV_CONFIG ?= conf/server.local.json
DEV_LOG ?= /tmp/video_server_dev.log
GATEWAY_CONFIG ?= conf/gateway.local.json
SERVICES_CONFIG ?= conf/services.local.json
USER_SERVICE_CONFIG ?= conf/user_service.local.json
DB_MIGRATION_CONFIG ?= $(USER_SERVICE_CONFIG)
VIDEO_SERVICE_CONFIG ?= conf/video_service.local.json
FILE_SERVICE_CONFIG ?= conf/file_service.local.json
TRANSCODE_SERVICE_CONFIG ?= conf/transcode_service.local.json
GATEWAY_LOG ?= /tmp/api_gateway_dev.log
USER_SERVICE_LOG ?= /tmp/user_service_dev.log
VIDEO_SERVICE_LOG ?= /tmp/video_service_dev.log
FILE_SERVICE_LOG ?= /tmp/file_service_dev.log
TRANSCODE_SERVICE_LOG ?= /tmp/transcode_service_dev.log
MARIADB_TOOL ?= tools/dev_mariadb.sh
REDIS_TOOL ?= tools/dev_redis.sh
FFMPEG_TOOL ?= tools/dev_ffmpeg.sh
REFERENCE_INFRA_TOOL ?= tools/dev_reference_infra.sh
CMAKE_BUILD_DIR ?= build/reference-runtime
CMAKE_BUILD_JOBS ?= 2
API_GATEWAY_BIN ?= $(CMAKE_BUILD_DIR)/svc_gateway/api_gateway
USER_SERVICE_BIN ?= $(CMAKE_BUILD_DIR)/svc_user/user_service
VIDEO_SERVICE_BIN ?= $(CMAKE_BUILD_DIR)/svc_video/video_service
FILE_SERVICE_BIN ?= $(CMAKE_BUILD_DIR)/svc_file/file_service
TRANSCODE_SERVICE_BIN ?= $(CMAKE_BUILD_DIR)/svc_transcode/transcode_service
BASE_URL ?= http://127.0.0.1:10000
CORE_SOURCES = server/common/auth.cc server/common/email_verification.cc \
		server/common/config.cc \
		server/common/redis_session_manager.cc \
		server/common/session_token.cc \
		server/database/database.cc \
		server/data/video.cc server/repository/admin_repository.cc \
		server/common/util.cc \
		server/common/bitelog.cc
USER_SOURCES = server/svc_user/source/user_repository.cc \
        server/svc_user/source/cached_user_repository.cc \
		server/common/password_hash.cc
VIDEO_SOURCES = server/svc_video/source/video_repository.cc server/common/outbox.cc
HTTP_BASE_SOURCES = server/common/http_server.cc server/common/route_support.cc \
        server/common/smoke_routes.cc
USER_ROUTE_SOURCES = server/svc_user/source/user_routes.cc
VIDEO_ROUTE_SOURCES = server/svc_video/source/video_routes.cc \
        server/svc_video/source/interaction_routes.cc
COMPAT_HTTP_SOURCES = $(HTTP_BASE_SOURCES) $(USER_ROUTE_SOURCES) $(VIDEO_ROUTE_SOURCES) \
        server/common/compatibility_http_server.cc
COMMON_LIBS = -L/usr/lib -ljsoncpp -lfmt -lspdlog -lodb-mysql -lodb \
		-lmysqlclient -lcpp-httplib -lhiredis -lcrypto -pthread

server: server/common/server_main.cc $(COMPAT_HTTP_SOURCES) \
		$(CORE_SOURCES) $(USER_SOURCES) $(VIDEO_SOURCES)
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o video_server \
		$(COMMON_LIBS)

user_service: server/svc_user/source/main.cc server/svc_user/source/svc_server.cc \
		server/svc_user/source/svc_data.cc \
		server/svc_user/source/cached_user_repository.cc \
		server/svc_user/source/svc_rpc.cc server/svc_user/source/svc_sync.cc \
		server/svc_user/source/svc_mq.cc $(HTTP_BASE_SOURCES) $(USER_ROUTE_SOURCES) \
		$(CORE_SOURCES) $(USER_SOURCES)
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o user_service \
		$(COMMON_LIBS)

video_service: server/svc_video/source/main.cc server/svc_video/source/svc_server.cc \
		server/svc_video/source/svc_data.cc \
		server/svc_video/source/svc_rpc.cc server/svc_video/source/svc_sync.cc \
		server/svc_video/source/svc_mq.cc $(HTTP_BASE_SOURCES) $(VIDEO_ROUTE_SOURCES) \
		$(CORE_SOURCES) $(VIDEO_SOURCES)
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o video_service \
		$(COMMON_LIBS)

file_service: server/svc_file/source/main.cc server/svc_file/source/svc_server.cc \
		server/svc_file/source/svc_data.cc server/svc_file/source/svc_rpc.cc \
		server/svc_file/source/svc_sync.cc server/svc_file/source/svc_mq.cc \
		server/common/session_token.cc server/common/config.cc server/common/object_storage.cc \
		server/common/util.cc server/common/bitelog.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o file_service \
		-L/usr/lib -ljsoncpp -lfmt -lspdlog -lcpp-httplib -lcrypto -pthread

transcode_service: server/svc_transcode/source/main.cc \
		server/svc_transcode/source/svc_server.cc \
		server/svc_transcode/source/svc_data.cc server/svc_transcode/source/svc_rpc.cc \
		server/svc_transcode/source/svc_worker.cc server/common/auth.cc \
		server/common/config.cc server/common/util.cc server/common/bitelog.cc \
		server/common/outbox.cc server/common/outbox_dispatcher.cc \
		server/database/database.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o transcode_service \
		-L/usr/lib -ljsoncpp -lfmt -lspdlog -lcpp-httplib -lodb-mysql \
		-lodb -lmysqlclient -pthread

interaction_service: server/common/service_main.cc $(COMPAT_HTTP_SOURCES) \
		$(CORE_SOURCES) $(VIDEO_SOURCES)
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o interaction_service \
		$(COMMON_LIBS)

api_gateway: server/svc_gateway/source/main.cc \
		server/svc_gateway/source/svc_server.cc \
		server/svc_gateway/source/svc_data.cc server/svc_gateway/source/svc_rpc.cc \
		server/common/http_client.cc server/common/auth.cc server/common/config.cc server/common/service_registry.cc \
		server/common/redis_session_manager.cc server/common/session_token.cc \
		server/common/util.cc server/common/bitelog.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o api_gateway \
		-L/usr/lib -ljsoncpp -lfmt -lspdlog -lcpp-httplib \
		-lhiredis -lcrypto -pthread

microservices: cmake-build

# CMake is authoritative for the reference-runtime branch. The direct g++
# targets remain available while individual services migrate to brpc.
cmake-configure:
	cmake -S server -B $(CMAKE_BUILD_DIR) -DCMAKE_BUILD_TYPE=RelWithDebInfo -DVOD_ENABLE_REFERENCE_RUNTIME=ON

cmake-build: cmake-configure
	cmake --build $(CMAKE_BUILD_DIR) --parallel $(CMAKE_BUILD_JOBS)

reference-infra-bootstrap:
	@bash $(REFERENCE_INFRA_TOOL) bootstrap

reference-infra-start:
	@bash $(REFERENCE_INFRA_TOOL) start

reference-infra-stop:
	@bash $(REFERENCE_INFRA_TOOL) stop

reference-infra-status:
	@bash $(REFERENCE_INFRA_TOOL) status

migrate: server/database/migrate_main.cc server/common/config.cc \
		server/database/database.cc server/common/util.cc server/common/bitelog.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o database_migrate \
		-L/usr/lib -ljsoncpp -lfmt -lspdlog -lodb-mysql -lodb \
		-lmysqlclient -lcpp-httplib -pthread

# Run the project's current automated test suite from one stable entry point.
test:
	$(MAKE) -C test/auth test
	$(MAKE) -C test/util test
	$(MAKE) -C test/config test
	$(MAKE) -C test/http test
	$(MAKE) -C test/database test
	$(MAKE) -C test/repository test
	$(MAKE) -C test/transcode test
	$(MAKE) -C test/outbox test
	$(MAKE) -C test/object_storage test

integration-test:
	$(MAKE) -C test/integration test

integration-redis:
	$(MAKE) -C test/integration redis

integration-mysql:
	$(MAKE) -C test/integration mysql

integration-gateway-auth: api_gateway
	$(MAKE) -C test/integration gateway-auth

redis-tsan:
	$(MAKE) -C test/integration redis-tsan

# Check whether the backend still covers the expected client route contract.
audit-routes:
	python3 tools/audit_routes.py

# Run a live smoke test against a running server.
# Usage: make smoke BASE_URL=http://192.168.19.129:9000
smoke:
	python3 tools/smoke_api.py --base-url $(BASE_URL)

# Compile and start the development server in the background.
dev-start: server
	@if [ ! -f "$(DEV_CONFIG)" ]; then \
		echo "$(DEV_CONFIG) not found. Copy conf/server.json and fill database settings first."; \
		exit 1; \
	fi
	@pkill -x video_server 2>/dev/null || true
	@VIDEO_ENABLE_SMOKE_CLEANUP=1 setsid -f ./video_server "$(DEV_CONFIG)" > "$(DEV_LOG)" 2>&1 < /dev/null
	@sleep 1
	@$(MAKE) dev-status

# Stop the development server if it is running.
dev-stop:
	@pkill -x video_server 2>/dev/null || true
	@echo "video_server stopped"

# Show whether the development server is running and whether /health responds.
dev-status:
	@pgrep -a video_server || { echo "video_server is not running"; exit 1; }
	@curl -fsS "$(BASE_URL)/health" >/dev/null && echo "health check ok: $(BASE_URL)"

# Start the development server if needed, then run the live smoke test.
dev-smoke:
	@if ! pgrep -x video_server >/dev/null; then $(MAKE) dev-start; fi
	@$(MAKE) smoke BASE_URL=$(BASE_URL)

# Start the development server if needed, then verify upload/avatar/static files.
dev-smoke-write:
	@if ! pgrep -x video_server >/dev/null; then $(MAKE) dev-start; fi
	@python3 tools/smoke_api.py --base-url $(BASE_URL) --write-checks

# Install development infrastructure into the current user's home directory.
dev-db-bootstrap:
	@bash $(MARIADB_TOOL) bootstrap

dev-db-start:
	@bash $(MARIADB_TOOL) start

dev-db-stop:
	@bash $(MARIADB_TOOL) stop

dev-db-status:
	@bash $(MARIADB_TOOL) status

dev-db-migrate: migrate
	@bash $(MARIADB_TOOL) start
	@set -e; for file in migrations/*.sql; do \
		echo "applying $$file"; \
		./database_migrate "$(DB_MIGRATION_CONFIG)" "$$file"; \
	done

dev-redis-bootstrap:
	@bash $(REDIS_TOOL) bootstrap

dev-redis-start:
	@bash $(REDIS_TOOL) start

dev-redis-stop:
	@bash $(REDIS_TOOL) stop

dev-redis-status:
	@bash $(REDIS_TOOL) status

dev-ffmpeg-bootstrap:
	@bash $(FFMPEG_TOOL) bootstrap

dev-ffmpeg-status:
	@bash $(FFMPEG_TOOL) status

dev-infra-bootstrap:
	@$(MAKE) dev-db-bootstrap
	@$(MAKE) dev-redis-bootstrap
	@$(MAKE) dev-ffmpeg-bootstrap
	@$(MAKE) dev-db-migrate
	@$(MAKE) dev-redis-start

dev-infra-stop:
	@$(MAKE) dev-redis-stop
	@$(MAKE) dev-db-stop

# Compile and start the local lightweight microservice demo.
dev-start-ms: microservices
	@for file in "$(GATEWAY_CONFIG)" "$(SERVICES_CONFIG)" "$(USER_SERVICE_CONFIG)" "$(VIDEO_SERVICE_CONFIG)" "$(FILE_SERVICE_CONFIG)" "$(TRANSCODE_SERVICE_CONFIG)"; do \
		if [ ! -f "$$file" ]; then echo "$$file not found."; exit 1; fi; \
	done
	@bash $(MARIADB_TOOL) start
	@bash $(REDIS_TOOL) start
	@$(MAKE) dev-stop-ms >/dev/null || true
	@VIDEO_ENABLE_SMOKE_CLEANUP=1 setsid -f "$(USER_SERVICE_BIN)" "$(USER_SERVICE_CONFIG)" > "$(USER_SERVICE_LOG)" 2>&1 < /dev/null
	@VIDEO_ENABLE_SMOKE_CLEANUP=1 setsid -f "$(VIDEO_SERVICE_BIN)" "$(VIDEO_SERVICE_CONFIG)" > "$(VIDEO_SERVICE_LOG)" 2>&1 < /dev/null
	@VIDEO_ENABLE_SMOKE_CLEANUP=1 setsid -f "$(FILE_SERVICE_BIN)" "$(FILE_SERVICE_CONFIG)" > "$(FILE_SERVICE_LOG)" 2>&1 < /dev/null
	@setsid -f "$(TRANSCODE_SERVICE_BIN)" "$(TRANSCODE_SERVICE_CONFIG)" > "$(TRANSCODE_SERVICE_LOG)" 2>&1 < /dev/null
	@for url in \
		"http://127.0.0.1:10001/healthz" \
		"http://127.0.0.1:10002/healthz" \
		"http://127.0.0.1:10003/healthz" \
		"http://127.0.0.1:10004/healthz"; do \
		ready=0; \
		for attempt in $$(seq 1 20); do \
			if curl --max-time 1 -fsS "$$url" >/dev/null 2>&1; then ready=1; break; fi; \
			sleep 0.5; \
		done; \
		if [ "$$ready" -ne 1 ]; then echo "service did not become healthy: $$url"; exit 1; fi; \
	done
	@setsid -f "$(API_GATEWAY_BIN)" "$(GATEWAY_CONFIG)" "$(SERVICES_CONFIG)" > "$(GATEWAY_LOG)" 2>&1 < /dev/null
	@ready=0; for attempt in $$(seq 1 20); do \
		if curl --max-time 1 -fsS "http://127.0.0.1:10000/healthz" >/dev/null 2>&1; then ready=1; break; fi; \
		sleep 0.5; \
	done; \
	if [ "$$ready" -ne 1 ]; then echo "api_gateway did not become healthy"; exit 1; fi
	@$(MAKE) dev-status-ms

# Stop the local lightweight microservice demo.
dev-stop-ms:
	@pkill -x api_gateway 2>/dev/null || true
	@pkill -x user_service 2>/dev/null || true
	@pkill -x video_service 2>/dev/null || true
	@pkill -x file_service 2>/dev/null || true
	@pkill -f '/svc_transcode/[t]ranscode_service( |$$)' 2>/dev/null || true
	@pkill -f '^\./interaction_service ' 2>/dev/null || true
	@echo "microservices stopped"

# Show whether all local microservice demo processes and health endpoints work.
dev-status-ms:
	@bash $(MARIADB_TOOL) status
	@bash $(REDIS_TOOL) status
	@pgrep -a api_gateway || { echo "api_gateway is not running"; exit 1; }
	@pgrep -a user_service || { echo "user_service is not running"; exit 1; }
	@pgrep -a video_service || { echo "video_service is not running"; exit 1; }
	@pgrep -a file_service || { echo "file_service is not running"; exit 1; }
	@pgrep -af '/svc_transcode/transcode_service( |$$)' || { echo "transcode_service is not running"; exit 1; }
	@curl --max-time 3 -fsS "http://127.0.0.1:10000/healthz" >/dev/null && echo "api_gateway healthz ok"
	@curl --max-time 3 -fsS "http://127.0.0.1:10002/healthz" >/dev/null && echo "user_service healthz ok"
	@curl --max-time 3 -fsS "http://127.0.0.1:10003/healthz" >/dev/null && echo "video_service healthz ok"
	@curl --max-time 3 -fsS "http://127.0.0.1:10001/healthz" >/dev/null && echo "file_service healthz ok"
	@curl --max-time 3 -fsS "http://127.0.0.1:10004/healthz" >/dev/null && echo "transcode_service healthz ok"

dev-smoke-ms:
	@if ! pgrep -x api_gateway >/dev/null; then $(MAKE) dev-start-ms; fi
	@$(MAKE) smoke BASE_URL=$(BASE_URL)

dev-smoke-write-ms:
	@if ! pgrep -x api_gateway >/dev/null; then $(MAKE) dev-start-ms; fi
	@python3 tools/smoke_api.py --base-url $(BASE_URL) --write-checks

# Remove locally generated build artifacts.
clean:
	$(MAKE) -C test/auth clean
	$(MAKE) -C test/util clean
	$(MAKE) -C test/config clean
	$(MAKE) -C test/http clean
	$(MAKE) -C test/database clean
	$(MAKE) -C test/repository clean
	$(MAKE) -C test/transcode clean
	$(MAKE) -C test/integration clean
	$(MAKE) -C test/outbox clean
	$(MAKE) -C test/object_storage clean
	$(MAKE) -C example/spdlog clean
	rm -f video_server
	rm -f api_gateway
	rm -f user_service
	rm -f video_service
	rm -f file_service
	rm -f transcode_service
	rm -f interaction_service
	rm -f database_migrate
