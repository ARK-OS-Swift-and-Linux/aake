CC=gcc
CFLAGS=-O2 -Wall -Iinclude

SRCS=src/main.c src/ninja_ui.c src/blueprint.c src/packager.c src/asset_gen.c src/ark_parser.c
OBJS=src/main.o src/ninja_ui.o src/blueprint.o src/packager.o src/asset_gen.o src/ark_parser.o
TARGET=aake

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) -lm

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

src/asset_gen.o: src/asset_gen.c
	$(CC) $(CFLAGS) -c src/asset_gen.c -o src/asset_gen.o

clean:
	rm -f $(OBJS) $(TARGET)
