CXXFLAGS = -fopenmp -O3 -std=c++20 -Wall $(shell GraphicsMagick++-config --cppflags --cxxflags) -Iinclude
LIBS = -fopenmp $(shell GraphicsMagick++-config --ldflags --libs) -lsfml-graphics -lsfml-window -lsfml-system

HEADERS = image_viewer.h
OBJS = image_viewer.o

%.o: %.cpp $(HEADERS)
	g++ -c -o $@ $< $(CXXFLAGS)

image_viewer: $(OBJS)
	g++ -o $@ $(OBJS) $(LIBS)

install: image_viewer
	cp -f image_viewer /usr/local/bin

clean:
	rm -f image_viewer $(OBJS)

.PHONY: clean install
