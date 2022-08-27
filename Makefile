# 
# VERSION CHANGES
#

#BV=$(shell (git rev-list HEAD --count))
#BD=$(shell (date))
BV=1234
BD=today
SDLFLAGS=$(shell (sdl2-config --static-libs --cflags))
CFLAGS=  -Wall -O2 -DBUILD_VER="$(BV)" -DBUILD_DATE=\""$(BD)"\" -DFAKE_SERIAL=$(FAKE_SERIAL)
#CFLAGS=  -Wall -O0 -ggdb -g -DBUILD_VER="$(BV)" -DBUILD_DATE=\""$(BD)"\" -DFAKE_SERIAL=$(FAKE_SERIAL)
LIBS=-lSDL2_ttf
CC=gcc
GCC=g++

OBJ=gdm-8341-sdl

default: $(OBJ)
	@echo
	@echo

gdm-8341-sdl: gdm-8341-sdl.cpp
	@echo Build Release $(BV)
	@echo Build Date $(BD)
	${GCC} ${CFLAGS} $(COMPONENTS) gdm-8341-sdl.cpp $(SDLFLAGS) $(LIBS) ${OFILES} -o ${OBJ} 

clean:
	rm -v ${OBJ} 
