CXXFLAGS = -march=native -Ofast -std=c++20 -Wall -Iinclude
LIBS = -lsfml-graphics -lsfml-window -lsfml-system

HEADERS = image_viewer.h util.h
OBJS = image_viewer.o util.o

image_viewer.o: image_viewer.cpp image_viewer.h
	g++ -c -o $@ $< $(CXXFLAGS)

util.o: util.cpp util.h
	g++ -c -o $@ $< $(CXXFLAGS)

image_viewer: image_viewer.o util.o
	g++ -o $@ $^ $(LIBS)

install: image_viewer
	cp -f image_viewer /usr/local/bin

clean:
	rm -f image_viewer $(OBJS)

.PHONY: clean install
