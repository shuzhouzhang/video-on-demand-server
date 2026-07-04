.PHONY: server migrate microservices test audit-routes smoke dev-start dev-stop dev-status dev-smoke dev-smoke-write dev-start-ms dev-stop-ms dev-status-ms dev-smoke-ms dev-smoke-write-ms clean

DEV_CONFIG ?= conf/server.local.json
DEV_LOG ?= /tmp/video_server_dev.log
GATEWAY_CONFIG ?= conf/gateway.local.json
SERVICES_CONFIG ?= conf/services.local.json
USER_SERVICE_CONFIG ?= conf/user_service.local.json
VIDEO_SERVICE_CONFIG ?= conf/video_service.local.json
INTERACTION_SERVICE_CONFIG ?= conf/interaction_service.local.json
GATEWAY_LOG ?= /tmp/api_gateway_dev.log
USER_SERVICE_LOG ?= /tmp/user_service_dev.log
VIDEO_SERVICE_LOG ?= /tmp/video_service_dev.log
INTERACTION_SERVICE_LOG ?= /tmp/interaction_service_dev.log
BASE_URL ?= http://127.0.0.1:9000
COMMON_SOURCES = source/config.cc source/database.cc source/video.cc \
		source/video_repository.cc source/util.cc source/bitelog.cc
COMMON_LIBS = -L/usr/lib -ljsoncpp -lfmt -lspdlog -lodb-mysql -lodb \
		-lmysqlclient -lcpp-httplib -pthread

server: source/server_main.cc source/http_server.cc source/config.cc \
		source/database.cc source/video.cc source/video_repository.cc \
		source/util.cc source/bitelog.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o video_server \
		$(COMMON_LIBS)

user_service: source/service_main.cc source/http_server.cc $(COMMON_SOURCES)
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o user_service \
		$(COMMON_LIBS)

video_service: source/service_main.cc source/http_server.cc $(COMMON_SOURCES)
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o video_service \
		$(COMMON_LIBS)

interaction_service: source/service_main.cc source/http_server.cc $(COMMON_SOURCES)
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o interaction_service \
		$(COMMON_LIBS)

api_gateway: source/gateway_main.cc source/util.cc source/bitelog.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o api_gateway \
		-L/usr/lib -ljsoncpp -lfmt -lspdlog -lcpp-httplib -pthread

microservices: api_gateway user_service video_service interaction_service

migrate: source/migrate_main.cc source/config.cc source/database.cc \
		source/util.cc source/bitelog.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o database_migrate \
		$(COMMON_LIBS)

# Run the project's current automated test suite from one stable entry point.
test:
	$(MAKE) -C test/util test
	$(MAKE) -C test/config test
	$(MAKE) -C test/http test
	$(MAKE) -C test/database test

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

# Compile and start the local lightweight microservice demo.
dev-start-ms: microservices
	@for file in "$(GATEWAY_CONFIG)" "$(SERVICES_CONFIG)" "$(USER_SERVICE_CONFIG)" "$(VIDEO_SERVICE_CONFIG)" "$(INTERACTION_SERVICE_CONFIG)"; do \
		if [ ! -f "$$file" ]; then echo "$$file not found."; exit 1; fi; \
	done
	@$(MAKE) dev-stop-ms >/dev/null || true
	@VIDEO_ENABLE_SMOKE_CLEANUP=1 setsid -f ./user_service "$(USER_SERVICE_CONFIG)" > "$(USER_SERVICE_LOG)" 2>&1 < /dev/null
	@VIDEO_ENABLE_SMOKE_CLEANUP=1 setsid -f ./video_service "$(VIDEO_SERVICE_CONFIG)" > "$(VIDEO_SERVICE_LOG)" 2>&1 < /dev/null
	@VIDEO_ENABLE_SMOKE_CLEANUP=1 setsid -f ./interaction_service "$(INTERACTION_SERVICE_CONFIG)" > "$(INTERACTION_SERVICE_LOG)" 2>&1 < /dev/null
	@sleep 1
	@setsid -f ./api_gateway "$(GATEWAY_CONFIG)" "$(SERVICES_CONFIG)" > "$(GATEWAY_LOG)" 2>&1 < /dev/null
	@sleep 1
	@$(MAKE) dev-status-ms

# Stop the local lightweight microservice demo.
dev-stop-ms:
	@pkill -x api_gateway 2>/dev/null || true
	@pkill -x user_service 2>/dev/null || true
	@pkill -x video_service 2>/dev/null || true
	@pkill -f './interaction_service' 2>/dev/null || true
	@echo "microservices stopped"

# Show whether all local microservice demo processes and health endpoints work.
dev-status-ms:
	@pgrep -a api_gateway || { echo "api_gateway is not running"; exit 1; }
	@pgrep -a user_service || { echo "user_service is not running"; exit 1; }
	@pgrep -a video_service || { echo "video_service is not running"; exit 1; }
	@pgrep -af './interaction_service' || { echo "interaction_service is not running"; exit 1; }
	@curl -fsS "http://127.0.0.1:9000/healthz" >/dev/null && echo "api_gateway healthz ok"
	@curl -fsS "http://127.0.0.1:9101/healthz" >/dev/null && echo "user_service healthz ok"
	@curl -fsS "http://127.0.0.1:9102/healthz" >/dev/null && echo "video_service healthz ok"
	@curl -fsS "http://127.0.0.1:9103/healthz" >/dev/null && echo "interaction_service healthz ok"

dev-smoke-ms:
	@if ! pgrep -x api_gateway >/dev/null; then $(MAKE) dev-start-ms; fi
	@$(MAKE) smoke BASE_URL=$(BASE_URL)

dev-smoke-write-ms:
	@if ! pgrep -x api_gateway >/dev/null; then $(MAKE) dev-start-ms; fi
	@python3 tools/smoke_api.py --base-url $(BASE_URL) --write-checks

# Remove locally generated build artifacts.
clean:
	$(MAKE) -C test/util clean
	$(MAKE) -C test/config clean
	$(MAKE) -C test/http clean
	$(MAKE) -C test/database clean
	$(MAKE) -C example/spdlog clean
	rm -f video_server
	rm -f api_gateway
	rm -f user_service
	rm -f video_service
	rm -f interaction_service
	rm -f database_migrate
