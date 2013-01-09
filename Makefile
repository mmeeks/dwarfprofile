all:
	gcc -Wall -I /usr/include/libdwarf -g -O2 -ldw -o dwarfprofile dwarfprofile.c

clean:
	rm -f dwarfprofile