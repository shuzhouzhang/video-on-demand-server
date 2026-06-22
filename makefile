.PHONY: test clean

# Run the project's current automated test suite from one stable entry point.
test:
	$(MAKE) -C test/util test

# Remove locally generated build artifacts.
clean:
	$(MAKE) -C test/util clean
	$(MAKE) -C example/spdlog clean
