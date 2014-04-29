all: dwarfprofile
	@echo "compiled. Now run:"
	@echo "make test"

qa/%:qa/%.c
	gcc -Wall -g -O2 -o $@ $<

qa/multi-inline: qa/multi-inline.cxx qa/multi-inline1.cxx qa/multi-inline2.cxx qa/multi-inline3.cxx qa/multi-inline4.cxx qa/multi-inline5.cxx
	g++ -Wall -g -O2 -o $@ -lm $^

.PHONY:qa
qa : qa/small qa/small-inline qa/small-lex qa/multi-inline

dwarfprofile : dwarfprofile.cxx logging.cxx fstree.cxx logging.hxx
	g++ -Wall -I/opt/libreoffice/include -I. -g `pkg-config --cflags --libs glib-2.0` \
	    -O0 -ldw -o dwarfprofile dwarfprofile.cxx fstree.cxx logging.cxx

dwarfprofilec : dwarfprofile.c
	gcc -Wall -I/opt/libreoffice/include -I. -g `pkg-config --cflags --libs glib-2.0` \
	-ldw -o $@ $<

test: dwarfprofile qa
	./dwarfprofile -e qa/small
	./dwarfprofile -e qa/small-inline
	./dwarfprofile -e qa/small-lex
	./dwarfprofile -e qa/multi-inline

clean:
	rm -f dwarfprofile qa/small qa/small-inline
