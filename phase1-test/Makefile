CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
LDFLAGS = -lpthread

# Platform-specific settings
ifeq ($(OS),Windows_NT)
    PLATFORM_SRC = platform-windows.c
    LDFLAGS +=
    EXE = test_frame_capture.exe
else
    PLATFORM_SRC = platform-posix.c
    LDFLAGS += -lrt
    EXE = test_frame_capture
endif

SRCS = test_frame_capture.c $(PLATFORM_SRC)
OBJS = $(SRCS:.c=.o)

.PHONY: all clean

all: $(EXE)

$(EXE): $(OBJS)
	$(CC) $(OBJS) -o $(EXE) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(EXE) frame_*.png

# Check if stb_image_write.h exists
check-deps:
	@if [ ! -f stb_image_write.h ]; then \
		echo "ERROR: stb_image_write.h not found!"; \
		echo "Please download it from:"; \
		echo "https://github.com/nothings/stb/blob/master/stb_image_write.h"; \
		echo ""; \
		echo "Or run: wget https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h"; \
		exit 1; \
	fi

# Build with dependency check
build: check-deps all

