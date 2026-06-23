config_test: config_test.cc ../../source/config.cc ../../source/util.cc \
	../../source/bitelog.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o $@ \
		-ljsoncpp -lfmt -lspdlog -pthread

.PHONY: test clean
test: config_test
	./config_test

clean:
	rm -f config_test
