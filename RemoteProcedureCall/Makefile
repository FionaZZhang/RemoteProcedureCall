CC=cc
CFLAGS=-c -Wall
LDFLAGS=-lm
SRC=rpc.c
RPC_OBJ=rpc.o
SERVER_OBJ=server.o
CLIENT_OBJ=client.o

.PHONY: all clean

all: rpc-server rpc-client

rpc-server: $(RPC_OBJ) $(SERVER_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

rpc-client: $(RPC_OBJ) $(CLIENT_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(RPC_OBJ): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

$(SERVER_OBJ): server.c
	$(CC) $(CFLAGS) -o $@ $<

$(CLIENT_OBJ): client.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f *.o rpc-server rpc-client
