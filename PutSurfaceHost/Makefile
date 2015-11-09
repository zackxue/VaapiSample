CC = gcc
CFLAGS = -DHAVE_VA_X11 -g -lva -lva-x11 -lX11 -lglut
TARGET = PutSurfaceHost
SRC = $(wildcard ./*.c)
OBJ=$(patsubst %.c,%.o,$(SRC))
ALL:$(TARGET)

$(TARGET):$(OBJ)
	$(CC) -o $@ $< $(CFLAGS)

#$(TARGET):$(OBJ)
#	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -rfv PutSurfaceHost PutSurfaceHost.o
