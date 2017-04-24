#!/bin/bash

make hello-world hex TARGET=avr-RSS2 

avrdude -p m128rfa1 -c avr109 -P /dev/ttyUSB0 -b 38400 -e -U flash:w:bc.hex

