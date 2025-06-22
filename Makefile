# Name of the executable
TARGET = main

# Source files
SRCS = main.c

CC = gcc
CFLAGS = -Wall -g `pkg-config fuse3 --cflags`
LDFLAGS = `pkg-config fuse3 --libs` -lcurl

# Build target
all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Remove built files
clean:
	rm -f $(TARGET)

# Test the FUSE filesystem
test: all
	@mkdir -p mountdir
	@fusermount3 -u mountdir 2>/dev/null || true
	@sleep 0.1
	@./$(TARGET) mountdir &
	@sleep 0.1
	@ls mountdir
	@cat mountdir/ping
	@sleep 0.1
	@fusermount3 -u mountdir

mount:
	@mkdir -p mountdir
	@fusermount3 -u mountdir 2>/dev/null || true
	@sleep 0.1
	@./$(TARGET) mountdir &
	@sleep 0.1

unmount:
	@fusermount3 -u mountdir

