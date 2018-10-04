all: hack.o bench.o

hack.o: hack.c
	gcc -msse4.2 -mtune=intel -g -O3 -c hack.c

bench.o: bench.c
	gcc -g -O2 -c bench.c
