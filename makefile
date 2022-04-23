image_viewer: image_viewer.cpp image_viewer.h
	g++ -o image_viewer -O2 -std=c++20 -Wall $(shell GraphicsMagick++-config --cppflags --cxxflags --libs) -lsfml-graphics -lsfml-window -lsfml-system image_viewer.cpp
