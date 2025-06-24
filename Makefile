
TARGET = main
SRCS = fuse/main.c fuse/fuse_utils.c
OBJS = $(SRCS:.c=.o)

CC = gcc
CFLAGS = -Wall -g -std=c11 -D_DEFAULT_SOURCE `pkg-config fuse3 --cflags`
LDFLAGS = `pkg-config fuse3 --libs` -lcurl -lcjson



all: $(TARGET)
	$(MAKE) mount


# Instead of building from souces, we build from objects.
# Saving on having to rebuild everything everytime
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

mount:
	@mkdir -p mnt
	@fusermount3 -u mnt 2>/dev/null || true
	@sleep 0.1
	@./$(TARGET) mnt &
	@sleep 0.1

unmount:
	@fusermount3 -u mnt 2>/dev/null || true


# Declare commands
.PHONY: all clean mount unmount