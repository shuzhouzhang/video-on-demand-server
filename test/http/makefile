http_server_test: fake_repositories.h http_server_test.cc ../../server/common/http_server.cc \
	../../server/common/route_support.cc ../../server/common/smoke_routes.cc \
	../../server/common/compatibility_http_server.cc \
	../../server/svc_user/source/user_routes.cc \
	../../server/svc_video/source/video_routes.cc \
	../../server/svc_video/source/interaction_routes.cc \
	../../server/common/auth.cc ../../server/data/video.cc \
	../../server/common/email_verification.cc \
	../../server/common/session_token.cc \
	../../server/common/util.cc ../../server/common/bitelog.cc \
	redis_session_manager_stub.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $(filter %.cc,$^) -o $@ \
		-ljsoncpp -lfmt -lspdlog -lcpp-httplib -lcrypto -pthread

.PHONY: test clean
test: http_server_test
	./http_server_test

clean:
	rm -f http_server_test
