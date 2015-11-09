CC = gcc
CFLAGS= -DHAVE_VA_X11 -g $(CONDITION)
LDFLAGS= -lva -lva-glx -lX11 -lglut -lGL -lpthread -lva-x11
SRC=$(wildcard ./*.c)
OBJ=$(patsubst %.c,%.o,$(SRC))
TARGET=vaclient

unexport CONDITION

ALL:$(TARGET)

$(TARGET):$(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<

clean:
	rm -rfv *.o $(TARGET)
	rm -rfv *~
