# Name of the executable
TARGET = main

# Source files
SRCS = main.c

CC = gcc
CFLAGS = -Wall -g `pkg-config fuse3 --cflags`
LDFLAGS = `pkg-config fuse3 --libs`

# Build target
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Remove built files
clean:
	rm -f $(TARGET)

# Test the FUSE filesystem
test: all
	@mkdir -p mnt
	@fusermount3 -u mountdir 2>/dev/null || true
	@sleep 0.2
	@./$(TARGET) mnt &
	@sleep 0.2
	@ls mnt
	@cat mnt/hello.txt
	@sleep 0.2
	@fusermount3 -u mnt
