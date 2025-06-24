# Name of the executable
TARGET = main

# Source files
SRCS = main.c

CC = gcc
CFLAGS = -Wall -g `pkg-config fuse3 --cflags`
LDFLAGS = `pkg-config fuse3 --libs` -lcurl -lcjson

# Build target
all: $(TARGET)
	$(MAKE) mount

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Remove built files
clean:
	rm -f $(TARGET)

# Test the FUSE filesystem
test: all
	@mkdir -p mnt
	@fusermount3 -u mnt 2>/dev/null || true
	@sleep 0.1
	@./$(TARGET) mnt &
	@sleep 0.1
	@ls mnt
	@cat mnt/ping
	@sleep 0.1
	@fusermount3 -u mnt

mount:
	@mkdir -p mnt
	@fusermount3 -u mnt 2>/dev/null || true
	@sleep 0.1
	@./$(TARGET) mnt &
	@sleep 0.1

unmount:
	@fusermount3 -u mnt 2>/dev/null || true

