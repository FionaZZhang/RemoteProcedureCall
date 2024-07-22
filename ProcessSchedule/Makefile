EXE=allocate

CFLAGS=-Wall

all: $(EXE)

$(EXE): allocate.c
	cc $(CFLAGS) -o $(EXE) $<

clean:
	rm -f $(EXE) *.o

.PHONY: clean run

run: all
	./$(EXE)
