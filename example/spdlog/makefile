GFLAGS=-I../../source
log-test.:log_test.cc ../../source/bitelog.cc
	g++ $(GFLAGS) $^ -o $@ -lspdlog -lfmt -lgflags -lpthread
clean:
	rm -rf log-test.