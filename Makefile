BIN 	:= bin
SOURCE  := src
INCLUDE := include
RESULTS := results

CC := gcc

CFLAGS := -Wall -Werror -g -I$(INCLUDE)

all: $(BIN)/dataServer $(BIN)/remoteClient

$(BIN)/dataServer: $(SOURCE)/server.c $(SOURCE)/common.c $(SOURCE)/queue.c $(SOURCE)/file_info.c
	$(CC) $(CFLAGS) $^ -o $@ -lpthread

$(BIN)/remoteClient: $(SOURCE)/client.c $(SOURCE)/common.c $(SOURCE)/queue.c $(SOURCE)/file_info.c
	$(CC) $(CFLAGS) $^ -o $@ -lpthread

clean:
	rm -f $(BIN)/*
	rm -rf $(RESULTS)/*