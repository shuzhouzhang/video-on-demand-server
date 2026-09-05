CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic

.PHONY: test clean

test: object_storage_test
	./object_storage_test

object_storage_test: object_storage_test.cc ../../server/common/object_storage.cc ../../server/common/session_token.cc
	$(CXX) $(CXXFLAGS) $^ -o $@ -lcrypto -lfmt -lspdlog -pthread

clean:
	rm -f object_storage_test
