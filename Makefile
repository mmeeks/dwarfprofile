all: dwarfprofile
	@echo "compiled. Now run:"
	@echo "make test # or"
	@echo "dwarfprofile -e <path>"

qa/small : qa/small.c
	gcc -Wall -g -O2 -ldw -o qa/small qa/small.c

qa : qa/small

dwarfprofile : dwarfprofile.c
	gcc -Wall -g -O2 -ldw -o dwarfprofile dwarfprofile.c

test: dwarfprofile qa
	./dwarfprofile -e qa/small

clean:
	rm -f dwarfprofile qa/small