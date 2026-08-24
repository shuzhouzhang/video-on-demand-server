COMMON_SOURCES = ../../server/common/auth.cc \
	../../server/common/password_hash.cc \
	../../server/common/email_verification.cc ../../server/common/session_token.cc \
	../../server/common/redis_session_manager.cc ../../server/common/util.cc \
	../../server/common/bitelog.cc
DATABASE_SOURCES = ../../server/database/database.cc \
	../../server/data/video.cc \
	../../server/svc_user/source/user_repository.cc \
	../../server/svc_video/source/video_repository.cc \
	../../server/svc_transcode/source/svc_data.cc
LIBS = -L/usr/lib -ljsoncpp -lfmt -lspdlog -lodb-mysql -lodb \
	-lmysqlclient -lcpp-httplib -lhiredis -lcrypto -pthread

redis_session_manager_test: redis_session_manager_test.cc $(COMMON_SOURCES)
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o $@ $(LIBS)

backend_integration_test: backend_integration_test.cc $(COMMON_SOURCES) \
	$(DATABASE_SOURCES)
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o $@ $(LIBS)

.PHONY: redis mysql gateway-auth test redis-tsan clean
redis: redis_session_manager_test
	./redis_session_manager_test

mysql: backend_integration_test
	./backend_integration_test

gateway-auth:
	python3 gateway_auth_test.py

test: redis mysql gateway-auth

redis-tsan:
	g++ -std=c++17 -Wall -Wextra -pedantic -fsanitize=thread -g \
		redis_session_manager_test.cc $(COMMON_SOURCES) -o redis_session_manager_tsan \
		$(LIBS) -fsanitize=thread
	./redis_session_manager_tsan

clean:
	rm -f redis_session_manager_test backend_integration_test \
		redis_session_manager_tsan
