# DK-SpectraBitmap
 Interactive touchscreen bitmaps on LED wall art

_SpectraBitmap_ is a collection of colorful LED shapes with tons of unique and unexpected animations. You can even draw your own animations on the built-in touchscreen display and have them instantly morphed into large scale animations!

![Spectra Bitmap Touchscreen](/images/spectrabitmap_touchscreen.jpeg)

For this project I made 60 bitmap image-based animations drawn 6 different LED modules, each having 60 LEDs in different shapes.

![Spectra Bitmap LED Modules](/images/spectrabitmap_red.jpeg)

## Goal
This is an experimental project considering these unknowns:
- What is the most interesting LED arrangement? I made 6 different modules with 60 LEDs each to play with this idea.
   * Flat hexagon flower
   * Hanging globe ball
   * Rings projected on a canvas
   * Glass underdesk lighting
   * Flat hourglass shape
   * Mirrored rings in glass

- Bitmap animations - This idea intrigued me as making algorithmic LEDs animations can be time consuming. I found a Python library to convert simple drawings to byte arrays, and those can be played as animations across the LEDs.

- Touchscreen interface - There are several inexpensive touchscreen LCDs you can find on eBay for DIY electronic projects. I wanted to use one to display the bitmap information, and then had the idea of drawing colors on the display to create live animations across the LED modules. Getting this to work was extremely cool, and motivated me to get past several technical issues with it.

![Spectra Bitmap LED Shapes](/images/spectrabitmap_shapes.jpeg)

## Features
### External Power 
This was the first project I've done with a large number of LEDs which require more power than an Arduino can provide. 360 LEDs could draw over 20 amps if they were all fully on at once.  This project animates up to half the LEDs at once with colors less than full brightness, so a 10 amp power supply can easily power the project.  An external power supply is connected to the LEDs, and the Arduino logic is powered by a standard USB connection.

Once the project was complete I spliced the external power into a USB cable to take advantage of the Arduino's protective circuits and power everything together.

### XPT2046 Touchscreen
This is a cheap eBay-sourced 3.5" TFT LCD with resistive touch capabilities and uses SPI to communicate with the Arduino. Note that this is a 3.3v module and requires a logic level shifter, a mistake I made shifting the voltage down with resistors acting as a voltage divider. It turns out resistors are too slow for this and introduce voltage slew which causes all sorts of problems, so just get a proper 5v to 3.3v shifter for data signals!

### Rotary Encoders
I had some experience with interrupt-based digital encoders in previous projects but I wanted to use them here with this more ambitious project to control _Light & Dark_. The left dial controls _Dark_ a combination of strobing and blanking effects. The right dial controls _Light_ an ambient background of light and dithering effects. Holding the rotary dial pushbutton down adjusts the project brightness, and pressing both buttons together pauses animation playback.

### BPM beat detection
A portion of the touchscreen features beats-per-minute tap detection, which sets LED animations synched to music between 80bpm to 160bpm.

![Spectra Bitmap Hardware Peek](/images/spectrabitmap_inside.jpeg)

## Software
- [FastLED](https://github.com/FastLED/FastLED/wiki/Overview) - My favorite Arduino library for controlling large numbers of addressable LEDs
- [XPT2046_Touchscreen library](https://github.com/PaulStoffregen/XPT2046_Touchscreen) from the creator of the Teensy board
- LED animation bitmaps are stored as byte arrays in Arduino PROGMEM (flash memory)
- Python Pillow image library to convert png images to byte array data

## Hardware
- Arduino Mega equivalent (ATMEGA2560) - I used a small generic version of the mega bought off eBay just to have more memory for animations
- 3.5" ILI9341 TFT LCD with built in XPT2046 Touchscreen
- 360x WS2812b addressable LEDs (neopixels). I used both strips and rings for this.
- 2x rotary encoders with pushbuttons
- 2x 5v to 3.3v logic level shifters
- 5V 10A external power supply
- Lots of 3-pin wire and JST-XT connectors to string up the LED modules
- A small tin from the kitchen as a project enclosure

![Spectra Bitmap Room View](/images/spectrabitmap_room.jpeg)