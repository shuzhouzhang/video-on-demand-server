.PHONY: test clean

test: transcode_test
	./transcode_test

transcode_test: transcode_test.cc ../../server/svc_transcode/source/svc_worker.cc \
		../../server/common/bitelog.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o $@ -lfmt -lspdlog -pthread

clean:
	rm -f transcode_test
