#!/usr/bin/python

import os
import sys
import math
from PIL import Image


# FORMATTED HEX OUTPUT FUNCTIONS -------------------------------------------

hexLimit   = 0 # Total number of elements in array being printed
hexCounter = 0 # Current array element number, 0 to hexLimit-1
hexDigits  = 0 # Number of digits (after 0x) in array elements
hexColumns = 0 # Max number of elements to output before line wrap
hexColumn  = 0 # Current column number, 0 to hexColumns-1

# Initialize global counters for outputHex() function below
def hexReset(count, columns, digits):
	global hexLimit, hexCounter, hexDigits, hexColumns, hexColumn
	hexLimit   = count
	hexCounter = 0
	hexDigits  = digits
	hexColumns = columns
	hexColumn  = columns

# Write hex digit (with some formatting for C array) to stdout
def outputHex(n):
	global hexLimit, hexCounter, hexDigits, hexColumns, hexColumn
	if hexCounter > 0:
		sys.stdout.write(",")              # Comma-delimit prior item
		if hexColumn < (hexColumns - 1):   # If not last item on line,
			sys.stdout.write(" ")      # append space after comma
	hexColumn += 1                             # Increment column number
	if hexColumn >= hexColumns:                # Max column exceeded?
		sys.stdout.write("\n  ")           # Line wrap, indent
		hexColumn = 0                      # Reset column number
	sys.stdout.write("{0:#0{1}X}".format(n, hexDigits + 2))
	hexCounter += 1                            # Increment item counter
	if hexCounter >= hexLimit: sys.stdout.write(" }"); # Cap off table


# IMAGE CONVERSION ---------------------------------------------------------

def convertImage(filename):
	sys.stderr.write("Image processing: %s\n" % filename)

	try:
		# Image height should match NeoPixel strip length,
		# no conversion or runtime checks are performed.
		im       = Image.open(filename)
		im       = im.convert("RGB")
		pixels   = im.load()
		numWords = im.size[0] * im.size[1]
		#prefix   = path.splitext(path.split(filename)[1])[0]
		hexReset(numWords, 9, 4)

		sys.stdout.write("{ ")

		# Quantize 24-bit image to 16 bits:
		# RRRRRRRR GGGGGGGG BBBBBBBB -> RRRRRGGGGGGBBBBB
		for x in range(im.size[0]): # Column major
			for y in range(im.size[1]):
				p = pixels[x, y]
				outputHex(((p[0] & 0b11111000) << 8) |
					  ((p[1] & 0b11111100) << 3) |
					  ( p[2] >> 3))
		return 1 # Success
	except:
		sys.stderr.write("Not an image file (?)\n")

	return 0 # Fail


# MAIN ---------------------------------------------------------------------

if(len(sys.argv) < 2):
	sys.stderr.write("Specify a folder containing images\n")
	sys.exit()

sys.stdout.write("const uint16_t PROGMEM pixelData[][240] = {\n")
	
bitmapCount = 0	
for f, files in enumerate(os.listdir(sys.argv[1])):
	if os.path.isfile(os.path.join(sys.argv[1], files)):
		if(convertImage(os.path.join(sys.argv[1], files))):
			bitmapCount+=1
			if(f < len(os.listdir(sys.argv[1]))-1): sys.stdout.write(",\n")

sys.stdout.write("\n};\n\n")

sys.stdout.write("const uint8_t PROGMEM bitmapCount = %s;\n\n" % bitmapCount)

# Output 5- and 6-bit gamma tables
hexReset(32, 12, 2)
sys.stdout.write("const uint8_t PROGMEM gamma5[] = {")
for i in range(32):
	outputHex(int(math.pow(float(i)/31.0,2.7)*255.0+0.5))
sys.stdout.write(";\n\n")
hexReset(64, 12, 2)
sys.stdout.write("const uint8_t PROGMEM gamma6[] = {")
for i in range(64):
	outputHex(int(math.pow(float(i)/63.0,2.7)*255.0+0.5))
sys.stdout.write(";\n\n")
