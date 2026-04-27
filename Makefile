# Makefile for CMapNav (with OpenStreetMap support)
# Tested with GCC on Linux, macOS, and MinGW/MSYS2 on Windows.

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -std=c11
LDFLAGS = -lm
TARGET  = cmapnav

SRCS = main.c graph.c heap.c hashtable.c quadtree.c \
       pathfinding.c mapdata.c utils.c \
       json_parser.c osm_loader.c

OBJS = $(SRCS:.c=.o)

.PHONY: all clean valgrind

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) route_export.txt \
	      _overpass_query.tmp _osm_data.json

# Memory-leak check (requires valgrind installed)
valgrind: $(TARGET)
	valgrind --leak-check=full --track-origins=yes \
	         --error-exitcode=1 ./$(TARGET) < /dev/null
