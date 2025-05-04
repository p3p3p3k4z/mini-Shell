CC = gcc
CFLAGS = -Wall -Iinclude
SRC = src
OBJ = obj
BIN = bin

SRCS = $(SRC)/main.c $(SRC)/process.c $(SRC)/parser.c $(SRC)/utils.c
OBJS = $(SRC)/main.o $(SRC)/process.o $(SRC)/parser.o $(SRC)/utils.o
TARGET = $(BIN)/shell

all: $(TARGET)

$(TARGET): $(OBJS)
	@mkdir -p $(BIN)
	$(CC) $(OBJS) -o $(TARGET)

$(SRC)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(SRC)/*.o
	rm -rf $(BIN)
