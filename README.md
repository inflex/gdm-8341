# GwInstek GDM-8341 Multimeter OSD & Control for Linux
gdm-8341 Multimeter OSD & Control for linux with keyboard mode selection

# Requirements

You will require the SDL2 development lib in linux

The default (currently) linux kernel will not assign a /dev/ttyUSBx port
to this meter without modifying cp210x.c to include the VID:PID pair
and recompiling as a new kernel module.  Hopefully this will be added
in the future to the mainline kernel.

Because this meter locks the front panel during USB communications I have added keyboard controls for the common modes I use,  continuity, volts, diode and resistance.  Try win-alt-c/v/d/r respectively.

# Setup

Build	 

	(linux) make
	
# Usage
	
   
Run from the command line

	./gdm-8341-sdl -p /dev/ttyUSB0







# gdm-8341
