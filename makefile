CXXFLAGS = -march=native -Ofast -std=c++20 -Wall -Iinclude
LIBS = -lsfml-graphics -lsfml-window -lsfml-system

HEADERS = image_viewer.h util.h
OBJS = image_viewer.o util.o

%.o: %.cpp makefile
	g++ -c -o $@ $< $(CXXFLAGS)

image_viewer: $(OBJS)
	g++ -o $@ $(OBJS) $(LIBS)

install: image_viewer
	cp -f image_viewer /usr/local/bin

clean:
	rm -f image_viewer $(OBJS)

.PHONY: clean install
