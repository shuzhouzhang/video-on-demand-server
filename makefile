.PHONY: server migrate test audit-routes smoke dev-start dev-stop dev-status dev-smoke dev-smoke-write clean

DEV_CONFIG ?= conf/server.local.json
DEV_LOG ?= /tmp/video_server_dev.log
BASE_URL ?= http://127.0.0.1:9000

server: source/server_main.cc source/http_server.cc source/config.cc \
		source/database.cc source/video.cc source/video_repository.cc \
		source/util.cc source/bitelog.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o video_server \
		-L/usr/lib -ljsoncpp -lfmt -lspdlog -lodb-mysql -lodb \
		-lmysqlclient -pthread

migrate: source/migrate_main.cc source/config.cc source/database.cc \
		source/util.cc source/bitelog.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o database_migrate \
		-L/usr/lib -ljsoncpp -lfmt -lspdlog -lodb-mysql -lodb \
		-lmysqlclient -pthread

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

# Remove locally generated build artifacts.
clean:
	$(MAKE) -C test/util clean
	$(MAKE) -C test/config clean
	$(MAKE) -C test/http clean
	$(MAKE) -C test/database clean
	$(MAKE) -C example/spdlog clean
	rm -f video_server
	rm -f database_migrate
