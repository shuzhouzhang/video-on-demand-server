database_test: database_test.cc ../../source/database.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o $@ \
		-L/usr/lib -lodb-mysql -lodb -lmysqlclient -lfmt -lspdlog -pthread

.PHONY: test clean
test: database_test
	./database_test

clean:
	rm -f database_test
