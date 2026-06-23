.PHONY: server test clean

server: source/server_main.cc source/http_server.cc source/config.cc \
		source/util.cc source/bitelog.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o video_server \
		-ljsoncpp -lfmt -lspdlog -pthread

# Run the project's current automated test suite from one stable entry point.
test:
	$(MAKE) -C test/util test
	$(MAKE) -C test/config test
	$(MAKE) -C test/http test

# Remove locally generated build artifacts.
clean:
	$(MAKE) -C test/util clean
	$(MAKE) -C test/config clean
	$(MAKE) -C test/http clean
	$(MAKE) -C example/spdlog clean
	rm -f video_server
