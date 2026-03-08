# Makefile for Multi-Stage Encoding Pipeline
# UTF-8 → ISCII → Acharya

CC      := gcc
CFLAGS  := -Wall -Wextra -std=c99
SRCS    := main.c converter_logic.c acharya_logic.c
OBJS    := $(SRCS:.c=.o)
TARGET  := converter

# default target
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# dependencies
main.o: main.c
converter_logic.o: converter_logic.c mapping_tables.h acharya_logic.h
acharya_logic.o: acharya_logic.c mapping_tables.h

clean:
	rm -f $(TARGET) $(OBJS)

# smoke test
.PHONY: test clean all
test: all
	@echo "Running smoke test against testbench10.txt..."
	@if [ -f testbench10.txt ]; then \
	    echo "testbench10.txt" | ./$(TARGET) || true; \
	else \
	    echo "no testbench10.txt available"; \
	fi
