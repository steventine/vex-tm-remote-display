# Phase 3: H.264/HLS streaming server
# Usage:
#   make                        # native build
#   make build                  # check deps then build
#   make check-deps             # verify required libraries
#   make CROSS_COMPILE_WINDOWS=1            # cross-compile .exe
#   make CROSS_COMPILE_WINDOWS=1 installer  # cross-compile + NSIS installer
#   make HAVE_LIBAVCODEC=1                  # use libavcodec/libavformat instead of x264

ifdef CROSS_COMPILE_WINDOWS
    include Makefile.Windows
else
    # -----------------------------------------------------------------------
    # Native (Linux / macOS) build
    # -----------------------------------------------------------------------
    CC     = gcc
    CFLAGS = -Wall -Wextra -O2 -std=c11 -pthread
    LDFLAGS = -lpthread -lrt

    # -----------------------------------------------------------------------
    # JPEG library (for --mode mjpeg fallback)
    # -----------------------------------------------------------------------
    TURBOJPEG_CFLAGS := $(shell pkg-config --cflags libturbojpeg 2>/dev/null)
    TURBOJPEG_LIBS   := $(shell pkg-config --libs   libturbojpeg 2>/dev/null)

    ifneq ($(TURBOJPEG_CFLAGS),)
        CFLAGS  += -DHAVE_LIBJPEG_TURBO $(TURBOJPEG_CFLAGS)
        LDFLAGS += $(TURBOJPEG_LIBS)
    else
        JPEG_LIBS := $(shell pkg-config --libs libjpeg 2>/dev/null)
        ifneq ($(JPEG_LIBS),)
            LDFLAGS += $(JPEG_LIBS)
        else
            LDFLAGS += -ljpeg
        endif
    endif

    # -----------------------------------------------------------------------
    # H.264 / TS muxer: libavcodec path or default x264 path
    # -----------------------------------------------------------------------
    ifdef HAVE_LIBAVCODEC
        CFLAGS  += -DHAVE_LIBAVCODEC -DHAVE_LIBAVFORMAT \
                   $(shell pkg-config --cflags libavcodec libavformat libswscale)
        LDFLAGS += $(shell pkg-config --libs   libavcodec libavformat libswscale)
        H264_SRCS = h264_encoder_avcodec.c ts_muxer_avformat.c
    else
        X264_CFLAGS := $(shell pkg-config --cflags x264 2>/dev/null)
        X264_LIBS   := $(shell pkg-config --libs   x264 2>/dev/null)
        ifneq ($(X264_LIBS),)
            CFLAGS  += $(X264_CFLAGS)
            LDFLAGS += $(X264_LIBS)
        else
            LDFLAGS += -lx264
        endif
        H264_SRCS = h264_encoder_x264.c ts_muxer_custom.c
    endif

    PLATFORM_SRC = platform-posix.c

    EXE = tm_stream_server

    SRCS = tm_stream_server.c \
           http_server.c \
           jpeg_encoder.c \
           hls_server.c \
           yuv_convert.c \
           $(H264_SRCS) \
           $(PLATFORM_SRC)

    OBJS = $(SRCS:.c=.o)

    .PHONY: all clean check-deps build

    all: $(EXE) test_inject

    $(EXE): $(OBJS)
	$(CC) $(OBJS) -o $(EXE) $(LDFLAGS)

    %.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

    test_inject: test_inject.c
	$(CC) $(CFLAGS) test_inject.c -o test_inject -lm

    clean:
	rm -f $(OBJS) $(EXE) test_inject test_inject.o

    check-deps:
	@echo "=== Checking phase3-hls dependencies ==="
	@echo ""
	@echo "--- H.264 encoder ---"
	@if pkg-config --exists x264 2>/dev/null || ldconfig -p 2>/dev/null | grep -q libx264; then \
		echo "  x264:          FOUND"; \
	else \
		echo "  x264:          NOT FOUND"; \
		echo "    Install:  sudo apt install libx264-dev"; \
		MISSING=1; \
	fi
	@echo ""
	@echo "--- JPEG library (for --mode mjpeg fallback) ---"
	@if pkg-config --exists libturbojpeg 2>/dev/null; then \
		echo "  libjpeg-turbo: FOUND"; \
	elif pkg-config --exists libjpeg 2>/dev/null || [ -f /usr/include/jpeglib.h ]; then \
		echo "  libjpeg:       FOUND (libjpeg-turbo preferred)"; \
	else \
		echo "  libjpeg:       NOT FOUND"; \
		echo "    Install:  sudo apt install libjpeg-turbo8-dev"; \
		MISSING=1; \
	fi
	@if [ "$${MISSING}" = "1" ]; then echo ""; echo "Some dependencies are missing."; exit 1; fi
	@echo ""
	@echo "All required dependencies found."

    build: check-deps all

endif  # !CROSS_COMPILE_WINDOWS
