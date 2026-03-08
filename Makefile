# Simple Makefile for Custom Encoding Converter
# automatically generates cnj_scan.c from the .re source (if re2c is installed)

CC      := gcc
CFLAGS  := -I./include -Wall -Wextra
SRCS    := main.c cnj.c cnj_scan.c im.c imli.c glyphs.c acharya_logic.c converter_logic.c
OBJS    := $(SRCS:.c=.o)
TARGET  := converter

# rule to rebuild the scanner from the .re specification
cnj_scan.c: cnj_scan_re
	@if command -v re2c >/dev/null 2>&1; then \
	    echo "Generating cnj_scan.c from cnj_scan_re"; \
	    re2c cnj_scan_re -o cnj_scan.c; \
	else \
	    echo "warning: re2c not found, using existing cnj_scan.c"; \
	fi

# default target
all: $(TARGET)

$(TARGET): cnj_scan.c $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS) cnj_scan.c

# basic smoke test (user can supply a small file)
.PHONY: test
test: all
	@echo "Running smoke test against testbench10.txt (if present)..."
	@if [ -f testbench10.txt ]; then \
	    ./$(TARGET) < testbench10.txt || true; \
	else \
	    echo "no testbench10.txt available"; \
	fi
