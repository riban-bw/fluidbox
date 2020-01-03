fluidbox: fluidbox.cpp buttonhandler.hpp screen.hpp
	g++ -o fluidbox -I/usr/include/freetype2 -lfluidsynth -lribanfb -lwiringPi fluidbox.cpp
