# Check if we're cross-compiling for Windows
ifdef CROSS_COMPILE_WINDOWS
    include Makefile.Windows
else
    # Native build
    CC = gcc
    CFLAGS = -Wall -Wextra -O2 -std=c11 -pthread
    LDFLAGS = -lpthread

    # Check for libjpeg-turbo
    TURBOJPEG_CFLAGS = $(shell pkg-config --cflags libturbojpeg 2>/dev/null)
    TURBOJPEG_LDFLAGS = $(shell pkg-config --libs libturbojpeg 2>/dev/null)

    ifneq ($(TURBOJPEG_CFLAGS),)
        CFLAGS += -DHAVE_LIBJPEG_TURBO $(TURBOJPEG_CFLAGS)
        LDFLAGS += $(TURBOJPEG_LDFLAGS)
        TURBOJPEG = 1
    else
        # Fall back to standard libjpeg
        # Check if pkg-config can find it
        JPEG_LDFLAGS = $(shell pkg-config --libs libjpeg 2>/dev/null)
        ifneq ($(JPEG_LDFLAGS),)
            LDFLAGS += $(JPEG_LDFLAGS)
        else
            LDFLAGS += -ljpeg
        endif
        TURBOJPEG = 0
    endif

    # Platform-specific settings
    ifeq ($(OS),Windows_NT)
        PLATFORM_SRC = platform-windows.c
        EXE = tm_stream_server.exe
    else
        PLATFORM_SRC = platform-posix.c
        LDFLAGS += -lrt
        EXE = tm_stream_server
    endif
endif

ifndef CROSS_COMPILE_WINDOWS
    # Native build rules (only if not cross-compiling)
    SRCS = tm_stream_server.c http_server.c jpeg_encoder.c $(PLATFORM_SRC)
    OBJS = $(SRCS:.c=.o)

    .PHONY: all clean check-deps

    all: $(EXE)

    $(EXE): $(OBJS)
	$(CC) $(OBJS) -o $(EXE) $(LDFLAGS)

    %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

    clean:
	rm -f $(OBJS) $(EXE)
endif

ifndef CROSS_COMPILE_WINDOWS
    check-deps:
	@echo "Checking dependencies..."
	@if pkg-config --exists libturbojpeg 2>/dev/null; then \
		echo "✓ libjpeg-turbo found"; \
	else \
		echo "⚠ libjpeg-turbo not found, will use standard libjpeg"; \
		echo "  Install with: sudo apt install libjpeg-turbo8-dev"; \
	fi
	@if pkg-config --exists libjpeg 2>/dev/null || [ -f /usr/include/jpeglib.h ]; then \
		echo "✓ libjpeg found"; \
	else \
		echo "✗ libjpeg not found"; \
		echo "  Install with: sudo apt install libjpeg-dev"; \
		exit 1; \
	fi

    build: check-deps all
endif

