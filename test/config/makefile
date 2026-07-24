config_test: config_test.cc ../../server/common/config.cc \
	../../server/common/service_registry.cc ../../server/common/util.cc \
	../../server/common/bitelog.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o $@ \
		-ljsoncpp -lfmt -lspdlog -pthread

.PHONY: test clean
test: config_test
	./config_test

clean:
	rm -f config_test
