http_server_test: http_server_test.cc ../../source/http_server.cc \
	../../source/video.cc \
	../../source/util.cc ../../source/bitelog.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o $@ \
		-ljsoncpp -lfmt -lspdlog -pthread

.PHONY: test clean
test: http_server_test
	./http_server_test

clean:
	rm -f http_server_test
