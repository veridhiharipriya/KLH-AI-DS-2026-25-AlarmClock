CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
CPPFLAGS ?=
LDFLAGS ?=
TARGET := build/chronos
SOURCES := src/main.c

.PHONY: all clean run test

all: $(TARGET)

$(TARGET): $(SOURCES)
	@mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SOURCES) $(LDFLAGS) -pthread -o $@

run: $(TARGET)
	./$(TARGET)

test: $(TARGET)
	python3 tests/smoke_test.py ./$(TARGET)

clean:
	rm -rf build
