CXXFLAGS = -O3 -std=c++20 -Wall $(shell GraphicsMagick++-config --cppflags --cxxflags --ldflags)
LIBS = $(shell GraphicsMagick++-config --libs) -lsfml-graphics -lsfml-window -lsfml-system

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
