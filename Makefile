all:
	gcc -Wall -g -O2 -ldw -o dwarfprofile dwarfprofile.c

clean:
	rm -f dwarfprofile