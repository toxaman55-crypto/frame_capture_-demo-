CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = v4l2_capture

all: $(TARGET)

$(TARGET): v4l2_capture.c
	$(CC) $(CFLAGS) $< -o $@

clean:
    rm -f $(TARGET) frame_*.jpg

