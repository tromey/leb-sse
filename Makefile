all: bench

OPT =  -msse4.2 -mtune=intel -O3

bench: hack.o bench.o
	gcc -g -O2 -o bench hack.o bench.o

hack.o: hack.c
	gcc -g $(OPT) -c hack.c

bench.o: bench.c
	gcc -g -c bench.c

clean:
	rm -f bench hack.o bench.o
