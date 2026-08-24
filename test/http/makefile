http_server_test: http_server_test.cc ../../server/common/http_server.cc \
	../../server/common/auth.cc ../../server/data/video.cc \
	../../server/common/email_verification.cc \
	../../server/common/session_token.cc \
	../../server/common/util.cc ../../server/common/bitelog.cc \
	redis_session_manager_stub.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o $@ \
		-ljsoncpp -lfmt -lspdlog -lcpp-httplib -lcrypto -pthread

.PHONY: test clean
test: http_server_test
	./http_server_test

clean:
	rm -f http_server_test
