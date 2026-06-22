util_test: util_test.cc ../../source/util.cc ../../source/bitelog.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o $@ \
		-ljsoncpp -lfmt -lspdlog -pthread

.PHONY: test clean
test: util_test
	./util_test

clean:
	rm -f util_test
