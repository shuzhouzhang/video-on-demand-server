auth_test: auth_test.cc ../../server/common/auth.cc \
		../../server/common/password_hash.cc \
		../../server/common/email_verification.cc
	g++ -std=c++17 -Wall -Wextra -pedantic $^ -o $@ \
		-lcpp-httplib -lcrypto -pthread

.PHONY: test clean
test: auth_test
	./auth_test

clean:
	rm -f auth_test
