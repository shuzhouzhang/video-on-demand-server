.PHONY: server migrate test audit-routes smoke clean

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
	python3 tools/smoke_api.py --base-url $${BASE_URL:-http://127.0.0.1:9000}

# Remove locally generated build artifacts.
clean:
	$(MAKE) -C test/util clean
	$(MAKE) -C test/config clean
	$(MAKE) -C test/http clean
	$(MAKE) -C test/database clean
	$(MAKE) -C example/spdlog clean
	rm -f video_server
	rm -f database_migrate
