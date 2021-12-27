image_viewer: image_viewer.cpp image_viewer.h
	g++ -o image_viewer -std=c++20 -Wall -lsfml-graphics -lsfml-window -lsfml-system image_viewer.cpp
