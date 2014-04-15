all: dwarfprofile
	@echo "compiled. Now run:"
	@echo "make test"

qa/%:qa/%.c
	gcc -Wall -g -O2 -o $@ $<

.PHONY:qa
qa : qa/small qa/small-inline qa/small-lex

dwarfprofile : dwarfprofile.cxx logging.cxx logging.hxx
	g++ -Wall -I. -g `pkg-config --cflags --libs glib-2.0` \
	    -O0 -ldw -o dwarfprofile dwarfprofile.cxx logging.cxx

dwarfprofilec : dwarfprofile.c
	gcc -Wall -I. -g `pkg-config --cflags --libs glib-2.0` \
	-ldw -o $@ $<

test: dwarfprofile qa
	./dwarfprofile -e qa/small
	./dwarfprofile -e qa/small-inline
	./dwarfprofile -e qa/small-lex

clean:
	rm -f dwarfprofile qa/small qa/small-inline
