CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -pedantic

.PHONY: test clean

test: outbox_test
	./outbox_test

outbox_test: outbox_test.cc ../../server/common/outbox_dispatcher.cc
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -f outbox_test
