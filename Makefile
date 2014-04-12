all: dwarfprofile
	@echo "compiled. Now run:"
	@echo "make test # eg."
	@echo "dwarfprofile -c -e dwarfprofile > callgrind.txt"

qa/small : qa/small.c
	gcc -Wall -g -O2 -ldw -o qa/small qa/small.c

qa : qa/small

dwarfprofile : dwarfprofile.cxx logging.cxx logging.hxx
	g++ -Wall -I. -g `pkg-config --cflags --libs glib-2.0` \
	    -O0 -ldw -o dwarfprofile dwarfprofile.cxx logging.cxx

test: dwarfprofile qa
	./dwarfprofile -e qa/small

clean:
	rm -f dwarfprofile qa/small
