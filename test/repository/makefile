repository_test: repository_test.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $< -o $@ -lfmt -lspdlog -pthread

.PHONY: test clean
test: repository_test
	./repository_test

clean:
	rm -f repository_test
