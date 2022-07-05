CXXFLAGS = -O2 -std=c++20 -Wall $(shell Magick++-config --cppflags --cxxflags)
LIBS = $(shell Magick++-config --libs --ldflags) -lsfml-graphics -lsfml-window -lsfml-system

HEADERS = image_viewer.h
OBJS = image_viewer.o

%.o: %.cpp $(HEADERS)
	g++ -c -o $@ $< $(CXXFLAGS)

image_viewer: $(OBJS)
	g++ -o $@ $(OBJS) $(CXXFLAGS) $(LIBS)

install: image_viewer
	cp -f image_viewer /usr/local/bin

clean:
	rm -f image_viewer $(OBJS)

.PHONY: clean install
