.phony: all run clean

all: fluidbox fluidboxmanager

fluidbox: fluidbox.cpp buttonhandler.hpp screen.hpp
	g++ -o fluidbox -I/usr/include/freetype2 -lfluidsynth -lwiringPi -lfreetype ribanfblib/ribanfblib.cpp fluidbox.cpp

fluidboxmanager: fluidboxmanager.cpp buttonhandler.hpp screen.hpp
	g++ -o fluidboxmanager -I/usr/include/freetype2 -lwiringPi -lfreetype fluidboxmanager.cpp ribanfblib/ribanfblib.cpp

fluidbox.debug: fluidbox.cpp buttonhandler.hpp screen.hpp
	g++ -g -o fluidbox.debug -I/usr/include/freetype2 -lfluidsynth -lwiringPi -lfreetype ribanfblib/ribanfblib.cpp fluidbox.cpp

fluidboxmanager.debug: fluidboxmanager.cpp buttonhandler.hpp screen.hpp
	g++ -g -o fluidboxmanager.debug -I/usr/include/freetype2 -lwiringPi -lfreetype fluidboxmanager.cpp ribanfblib/ribanfblib.cpp

install: fluidbox fluidboxmanager fluidbox.service
	cp fluidbox.service /etc/systemd/system/fluidbox.service
	systemctl enable fluidbox.service

run: all
	./fluidboxmanager

clean:
	rm -f fluidbox
	rm -f fluidboxmanager
	rm -f fluidbox.debug
	rm -f fluidboxmanager.debg
