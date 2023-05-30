# definizione del compilatore e dei flag di compilazione
# che vengono usate dalle regole implicite
CC=gcc
CFLAGS=-g -Wall -O -std=c11
LDLIBS=-lm -lrt -pthread 

# eseguibili da costruire
EXECS=archivio client1 

.PHONY: clean

# di default make cerca di realizzare il primo target 
all: $(EXECS)

# non devo scrivere il comando associato ad ogni target 
# perché il defualt di make in questo caso va bene

archivio: archivio.o xerrori.o
archivio.o: archivio.c xerrori.h
xerrori.o: xerrori.c xerrori.h


client1: client1.o xerrori.o
client1.o: client1.c xerrori.h
 
# target che cancella eseguibili e file oggetto
clean:
	rm -f $(EXECS) *.o  