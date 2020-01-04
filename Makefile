fluidbox: fluidbox.cpp buttonhandler.hpp screen.hpp
	g++ -o fluidbox -I/usr/include/freetype2 -lfluidsynth -lribanfb -lwiringPi fluidbox.cpp

fluidboxmanager: fluidboxmanager.cpp buttonhandler.hpp screen.hpp
	g++ -o fluidboxmanager -I/usr/include/freetype2 -lribanfb -lwiringPi fluidboxmanager.cpp

.phony: all run

all: fluidbox fluidboxmanager

run: all
	./fluidboxmanager

install: fluidbox fluidboxmanager fluidbox.service
	cp fluidbox.service /etc/systemd/system/fluidbox.service
	systemctl enable fluidbox.service

