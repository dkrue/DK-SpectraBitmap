#include <FastLED.h>
#include <Adafruit_GFX.h>
#include <SPI.h>
#include <Wire.h> // this is needed outside of Arduino IDE even tho we aren't using it
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <Encoder.h>
#include "pixelData.h"

#define UINT8_BITS 8

/*** TOUCHSCREEN LCD ***/
// Calibration data for the raw touch data to the screen coordinates
// DK mirror X axis on cheap chinese version in addition to rotation(2)
#define TS_MINX 3950//150
#define TS_MINY 50
#define TS_MAXX 150//3800
#define TS_MAXY 3800

// The XPT2046 uses hardware SPI on the shield, and #8
#define XPT_CS 15 //was 8 on uno
XPT2046_Touchscreen ts = XPT2046_Touchscreen(XPT_CS);

// The display also uses hardware SPI, plus 7, 9, 10
#define TFT_DC 11 // was 10 on uno
#define TFT_CS 7
#define TFT_RST 9
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);

// Size of the color selection boxes and the paintbrush size
#define BOXSIZE 30
#define SLIDERSIZE 32 // sliders range from 0 to (2*32)
#define DIVIDERSIZE 20
#define PENRADIUS 5
#define BITMAPSCALE 10
byte colorSelectorCount = 7, currentDrawMode = 0; // add to animation, or palette drawing only
uint16_t colorSelector[] = {ILI9341_RED, ILI9341_YELLOW, ILI9341_GREEN, ILI9341_WHITE, ILI9341_BLACK, ILI9341_BLUE, ILI9341_MAGENTA};
uint16_t currentDrawColor[] = {ILI9341_RED, ILI9341_BLACK};
uint16_t paintedBitmap[240];

/*** ROTARY ENCODERS (on interrupt pins) ***/
Encoder darkKnob(21, 20); 
Encoder lightKnob(18, 19);
uint32_t darkKnobPosition, darkKnobPrev;
uint32_t lightKnobPosition, lightKnobPrev;
#define darkKnobButton 34
#define lightKnobButton 32
uint32_t pauseTime = 0;
boolean pauseState;

/*** NEOPIXEL STRIP MODULES ***/
#define MODULE_COUNT 6 
#define MAX_PARALLEL_MODULES 3
#define LEDS_PER_MODULE 60
CRGB leds[MODULE_COUNT][LEDS_PER_MODULE];
byte modulePatterns[] = {B11100000, B11000000, B10100000, B01100000, B01110000, B01010000, B10110000, B11010000, B00110000, B10010000, B11001000, B10101000, B01101000, B01110000, B10001100, B00110100, B01000000, B10000000};
byte moduleActivePattern = modulePatterns[0], activeModuleIndex = 0;
byte activeModuleCount = 1;
byte moduleBitmask = B10000000;

uint8_t animationIndex = 0;
uint16_t *pixelBaseAddr = pixelData[animationIndex], *prevPixelBaseAddr = pixelData[animationIndex];

uint8_t bitmapHeight = 10, bitmapSize = sizeof(pixelData[0]) / 2, bitmapColumnIndex = 0;
uint8_t bitmapWidth = bitmapSize / bitmapHeight;
uint8_t stripingCount = LEDS_PER_MODULE / bitmapHeight;
bool mirrorOddStripingAnimation = false;

// Randomized/controllable rates and modes
byte smoothingMode = 2, smoothingRate = 2; // (smoothingRate range: 2 to 4) [higher is slower]
byte animationResetMode = 2, animationResetRange = 4; // (range: 2 to 8) (higher is closer to animation start)
byte animationLoopRate, animationLoopCounter = 0, animationRateDivisor = 2; // loop bitmap every 2/4/8/16 beats
byte refreshRate = 60; // 60fps (range: 15-60 for different effects)
byte stripingIndex = 0, stripingModulo = 2; // range (1 to module count, where module=groups of 10 leds)
byte stripingAdvance = 1;
byte backgroundHue = 0, backgroundValue = 0;
byte strobeAmount=0, strobeState, strobeRate=8, strobeCounter, strobePatternIndex;
byte strobeModuleAdvance, strobeActiveModule;
byte strobePattern[8] = {B10101010, B10100010, B11001011, B11110101, B01100101, B10101010, B00000101, B10100010};
byte strobeBitmask = B10000000;

uint8_t darkSlider = 0, lightSlider = 0, brightSlider = 60;
uint8_t bpmAverage = 130;

uint32_t currentMillis = 0, prevMillis = 0, prevAnimFrameMillis = 0, prevSceneMillis = 0, touchMillis = 0, prevTouchMillis = 0;

void setup() {

  pinMode(darkKnobButton, INPUT_PULLUP);
  pinMode(lightKnobButton, INPUT_PULLUP);

  randomSeed(analogRead(A5));
  random16_add_entropy( random());

  // Initialize LCD and touchscreen
  tft.begin();

  if (!ts.begin()) {
    while (1);
  }
  ts.setRotation(2);

  tft.fillScreen(ILI9341_BLACK);
  drawColorSelector();

  // Initialize Neopixel LEDs
  FastLED.addLeds<NEOPIXEL, 25>(leds[0], LEDS_PER_MODULE)
    .setCorrection(TypicalSMD5050).setTemperature(Tungsten40W); //hexflower
  FastLED.addLeds<NEOPIXEL, 27>(leds[1], LEDS_PER_MODULE)
    .setCorrection(TypicalSMD5050).setTemperature(Tungsten40W); //sprials
  FastLED.addLeds<NEOPIXEL, 29>(leds[2], LEDS_PER_MODULE)
    .setCorrection(TypicalSMD5050).setTemperature(Candle); //sphere
  FastLED.addLeds<NEOPIXEL, 31>(leds[3], LEDS_PER_MODULE)
    .setCorrection(TypicalSMD5050).setTemperature(Candle); //belljar    
  FastLED.addLeds<NEOPIXEL, 30>(leds[4], LEDS_PER_MODULE)
    .setCorrection(TypicalSMD5050).setTemperature(Tungsten40W); //hourglass      
  FastLED.addLeds<NEOPIXEL, 28>(leds[5], LEDS_PER_MODULE)
    .setCorrection(TypicalSMD5050).setTemperature(Tungsten40W); //TBD        

  newAnimationParameters();

  // Set initial bpm to 130bpm
  animationLoopRate = (uint32_t)(bpmAverage*1000ul) / 60 / bitmapWidth;
  animationLoopRate /= animationRateDivisor;

  // Touch screen interrupt for tap tempo while touchscreen not working
  //attachInterrupt(digitalPinToInterrupt(3), touchInterrupt, FALLING);
}

void loop() {
  currentMillis = millis();

  updateTouchScreen(); // why is touchscreen not working and/or sending random values
  updateEncoders();

  // Update neopixels at a constant frame rate (FPS = refreshRate)
  if(currentMillis - prevMillis > 1000 / refreshRate && pauseState == 0) {
    prevMillis = currentMillis;

    // Show LEDs rendered on prior pass.  Animation timing is more consistent this way
    #ifndef MAX_PARALLEL_MODULES
      FastLED.show();
    #endif

    // Check if the next animation frame should happen based on BPM -> animationLoopRate
    if(currentMillis - prevAnimFrameMillis > 1000 / animationLoopRate) {
      prevAnimFrameMillis = currentMillis;

      // ~~*~~ LIGHT ~~*~
      backgroundValue = lightSlider+20; // add to brightness for FastLED to do its HSV magic at low levels
      if(lightSlider < 3) backgroundValue = 0; // round variances down to off

      // If light slider has value, transform possibly blinky to smooth & bright:
      if(lightSlider > 4) {
        smoothingRate = (lightSlider >> 3) + 1; // to 1-8 (smooth and fade more light)
      }

      // ~~*~~ DARK ~~*~~
      strobeAmount = darkSlider * 2;
      strobeRate = 3 - ((darkSlider >> 3)%3); // map to 0-8 then two and a half ranges of 3-0 (lower strobes faster)
      if(darkSlider > 52) strobeRate -= 1; // fastest strobe at top of slider

      // If dark slider has value, take module advance off random and make it more likely to advance quickly
      if(darkSlider > 4 && strobeModuleAdvance <= 12) strobeModuleAdvance = 12 - (darkSlider >> 3); // map to 12-4 when not on all strobe

      // Draw animation cursor to LCD
      tft.drawFastHLine(bitmapColumnIndex * BITMAPSCALE + 1, BOXSIZE+DIVIDERSIZE, BITMAPSCALE, animationLoopCounter % 2 ? ILI9341_GREEN : ILI9341_RED);
      tft.drawFastHLine(bitmapColumnIndex * BITMAPSCALE + 1, bitmapHeight * BITMAPSCALE + 1 + BOXSIZE+DIVIDERSIZE, BITMAPSCALE, animationLoopCounter % 2 ? ILI9341_GREEN : ILI9341_RED);
    }

    for(uint8_t i=0; i<bitmapHeight; i++) {
      uint8_t r, g, b;

      // Bitmap animation pixel should be read this time around
      if(prevAnimFrameMillis == currentMillis) {

        uint16_t rgb = readBitmapPixel(i);

        // Expand 16-bit color to 24 bits using gamma tables
        // RRRRRGGGGGGBBBBB -> RRRRRRRR GGGGGGGG BBBBBBBB
        r = pgm_read_byte(&gamma5[ rgb >> 11        ]);
        g = pgm_read_byte(&gamma6[(rgb >>  5) & 0x3F]);
        b = pgm_read_byte(&gamma5[ rgb        & 0x1F]);
      }

      // Duplicate across each stripe (segment of 10 pixels)
      for(uint8_t j=0; j<stripingCount; j++) {

        uint8_t pixelIndex = i + j * bitmapHeight;

        // Mirror the animation on every other segment
        // This flows across segments differently and looks great on circular objects
        if(mirrorOddStripingAnimation && j%2==1) {
          pixelIndex = (bitmapHeight-1-i) + j * bitmapHeight;
        }

        // For each physical object module
        for(uint8_t m=0; m < MODULE_COUNT; m++) {

          // Bitshift 8-bit active module pattern from left side to know if module is active or not
          if(moduleActivePattern & moduleBitmask >> m) {
            setAndSmoothPixel(pixelIndex, prevAnimFrameMillis == currentMillis, m, j, r, g, b);
          } 
          #ifndef MAX_PARALLEL_MODULES
            else {
              //setAndSmoothPixel(pixelIndex, prevAnimFrameMillis == currentMillis, m, j, 0, 0, 0);
            }
          #endif
        }
      }
    }

    #ifdef MAX_PARALLEL_MODULES
      // Display only active modules for higher FPS
      for(uint8_t m=0; m < MODULE_COUNT; m++) {

        // Bitshift 8-bit active module pattern from left side to know if module is active or not
        if(moduleActivePattern & moduleBitmask >> m) {
          FastLED[m].showLeds();
        }
      }
    #endif

    // Advance strobe counter
    if(++strobeCounter % strobeRate == 0) {

      // Bitshift bitmask by one for next strobe state
      strobeBitmask >>= 1;
      if(strobeBitmask == 0) strobeBitmask = 128;

      strobeState = strobePattern[strobePatternIndex] & strobeBitmask;

      // Advance strobe to new module
      if(strobeCounter % (strobeRate*strobeModuleAdvance) == 0)
        strobeActiveModule = random(MODULE_COUNT);

      // Randomly select new strobe pattern sometimes
      if(random(150) == 0) {
        strobePatternIndex = random(8);
      }
    }

    // Advance bitmap animation column after calculating everything
    if(prevAnimFrameMillis == currentMillis) {
      bitmapColumnIndex++; // TODO: skip this sometimes when in slo mo mode?

      // All pixels drawn at this point
      if(bitmapColumnIndex >= bitmapWidth) { // End of animation table reached
        resetAnimation();
      }
    }

    // Update LCD framerate display
    currentMillis = 1000 / (millis() - prevMillis);
    tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK); tft.setTextSize(2);
    tft.setCursor(1, BOXSIZE+2);
    //if (currentMillis<100) tft.print('0');
    if (currentMillis<10) tft.print('0');
    tft.print(currentMillis);
    tft.setTextSize(1);
    tft.print(F("FPS"));
  }
} // end main loop


// Read animation pixel color from PROGMEM table
uint16_t readBitmapPixel(uint8_t rowIndex) {
  uint16_t rgb = 0;

  // Animation from painted touchscreen has precedence
  rgb = paintedBitmap[(bitmapColumnIndex * bitmapHeight) + rowIndex];

  // If draw mode is unlocked then blend with premade animation
  if(currentDrawMode == 0) {
    // If recently transitioned from new scene, have a good chance of showing previous colors
    // at the beginning of the animation loop - transition smoothing effect
    if(rgb == 0 && random8((animationLoopCounter+2) * 2) < 3) {
      rgb = pgm_read_word(&prevPixelBaseAddr[(bitmapColumnIndex * bitmapHeight) + rowIndex]);
    }
    if(rgb == 0) {
      rgb = pgm_read_word(&pixelBaseAddr[(bitmapColumnIndex * bitmapHeight) + rowIndex]);
    }
  }

  // Draw scaled bitmap to LCD
  tft.fillRect(bitmapColumnIndex * BITMAPSCALE + 1, rowIndex * BITMAPSCALE + 1 + BOXSIZE+DIVIDERSIZE, BITMAPSCALE, BITMAPSCALE, rgb);

  return rgb;
}

void setAndSmoothPixel(uint8_t pixelIndex, boolean animationUpdated, uint8_t m, uint8_t j, uint8_t setR, uint8_t setG, uint8_t setB) {

  uint8_t xR = leds[m][pixelIndex].r;
  uint8_t xG = leds[m][pixelIndex].g;
  uint8_t xB = leds[m][pixelIndex].b;

  // Bitmap animation was not read this frame, use current pixel value
  if(!animationUpdated)
  {
    setR = xR;
    setG = xG;
    setB = xB;
  }
  // (OR if this stripe should not be animated due to stripingModulo / sweep index value)
  else if(j < stripingIndex || (j>=stripingIndex? j-stripingIndex : j) % stripingModulo > 0) {
    setR = xR > 0 ? xR - random(2): 0; // -1: decrement a tiny amount to trigger smoothing fade
    setG = xG > 0 ? xG - 1: 0; // random(2): fade tracers only active if the color was there to begin with
    setB = xB > 0 ? xB - random(2): 0;
  }

  // smoothingMode is ranked by increasing intensity
  switch(smoothingMode) {

    case 0: // Linear fade on / linear off
    xR = setR > xR ? (setR>>(4-smoothingRate))+xR : (xR > 18-(smoothingRate*4) ? xR-((18-smoothingRate*4)) : 0);
    xG = setG > xG ? (setG>>(4-smoothingRate))+xG : (xG > 18-(smoothingRate*4) ? xG-((18-smoothingRate*4)) : 0);
    xB = setB > xB ? (setB>>(4-smoothingRate))+xB : (xB > 18-(smoothingRate*4) ? xB-((18-smoothingRate*4)) : 0);
    break;

    case 1: // Exponential on, exponential fade
    xR = setR > xR ? setR>>(4-smoothingRate) : (setR < xR ? xR>>(4-smoothingRate) : xR);
    xG = setG > xG ? setG>>(4-smoothingRate) : (setG < xG ? xG>>(4-smoothingRate) : xG);
    xB = setB > xB ? setB>>(4-smoothingRate) : (setB < xB ? xB>>(4-smoothingRate) : xB);
    break;

    case 2: // Twinkle linear on, soft exponential fade
    xR = setR > xR+1 ? xR+random(3) : xR>>(4-smoothingRate);
    xG = setG > xG+1 ? xG+random(3) : xG>>(4-smoothingRate);
    xB = setB > xB+1 ? xB+random(3) : xB>>(4-smoothingRate);
    break;

    case 3: // Alternating pixel exponential fade on / linear off
    xR = setR >= xR ? (pixelIndex % 2 == 0 ? (setR>>smoothingRate)+xR : setR>>smoothingRate) : (xR > 18-(smoothingRate*4) ? xR-((18-smoothingRate*4)) : xR);
    xG = setG >= xG ? (pixelIndex % 2 == 0 ? (setG>>smoothingRate)+xG : setG>>smoothingRate) : (xG > 18-(smoothingRate*4) ? xG-((18-smoothingRate*4)) : xG);
    xB = setB >= xB ? (pixelIndex % 2 == 0 ? (setB>>smoothingRate)+xB : setB>>smoothingRate) : (xB > 18-(smoothingRate*4) ? xB-((18-smoothingRate*4)) : xB);
    break;

    case 4: // Exponential on, linear fade
    xR = setR >= xR ? setR>>smoothingRate : xR > 18-(smoothingRate*4) ? xR-((18-smoothingRate*4)) : 0;
    xG = setG >= xG ? setG>>smoothingRate : xG > 18-(smoothingRate*4) ? xG-((18-smoothingRate*4)) : 0;
    xB = setB >= xB ? setB>>smoothingRate : xB > 18-(smoothingRate*4) ? xB-((18-smoothingRate*4)) : 0;
    break;

    case 5: // Instant on, linear fade:
    xR = setR >= xR ? setR : xR > 18-(smoothingRate*4) ? xR-((18-smoothingRate*4)) : 0;
    xG = setG >= xG ? setG : xG > 18-(smoothingRate*4) ? xG-((18-smoothingRate*4)) : 0;
    xB = setB >= xB ? setB : xB > 18-(smoothingRate*4) ? xB-((18-smoothingRate*4)) : 0;
    break;

    case 6: // Instant on, soft exponential fade
    xR = setR >= xR ? setR : xR>>(4-smoothingRate);
    xG = setG >= xG ? setG : xG>>(4-smoothingRate);
    xB = setB >= xB ? setB : xB>>(4-smoothingRate);
    break;

    case 7:  // Instant on, strong linear fade
    xR = setR > xR ? setR : xR > 28-(smoothingRate*6) ? xR-((28-smoothingRate*6)) : 0;
    xG = setG > xG ? setG : xG > 28-(smoothingRate*6) ? xG-((28-smoothingRate*6)) : 0;
    xB = setB > xB ? setB : xB > 28-(smoothingRate*6) ? xB-((28-smoothingRate*6)) : 0;
    break;

    case 8:  // Instant on, hard exponential fade, strobe on equal value
    xR = setR > xR ? setR : xR>>(5-smoothingRate);
    xG = setG > xG ? setG : xG>>(5-smoothingRate);
    xB = setB > xB ? setB : xB>>(5-smoothingRate);
    break;

    default: // No smoothing
    xR = setR;
    xG = setG;
    xB = setB;
  }

  leds[m][pixelIndex].setRGB(xR, xG, xB);

  // Minimum background color - values <= 8 set increasingly fewer leds
  if(lightSlider > 8 || pixelIndex%(10-lightSlider) == 0) {
      // Clamp minimum background color as HSV brightness value based on "light" slider
      leds[m][pixelIndex] |= CHSV(backgroundHue, 255, backgroundValue);
  }

  // Final stage strobing
  // If active strobing module or all-strobe
  if(m == strobeActiveModule || strobeModuleAdvance > 14) {
    if(darkSlider < 32) {
      if(strobeState == 0){ // Color background strobing when dark slider less than halfway
        leds[m][pixelIndex] -= CHSV(0, 0, strobeAmount);
      }
    } else { // Up to full blast on fast strobe
      if(darkSlider > 48 || (strobeCounter % 64 > darkSlider)) { // strobe delay for delay between strobe sequences
        if(strobeState == 0) {
          leds[m][pixelIndex] = CRGB::Black;
        } else {
          leds[m][pixelIndex] += CHSV(0, 0, strobeAmount);
        }
      }
    }
  }

  // Master brightness control
  if(brightSlider < 60) {
    leds[m][pixelIndex].nscale8_video(brightSlider*4);
  }
}


void resetAnimation() {

    switch(animationResetMode) {
      case 0: // Restart animation at random point
      bitmapColumnIndex = random(bitmapWidth);
      break;

      case 1: // Restart animation at random point near beginning
      bitmapColumnIndex = random(bitmapWidth/animationResetRange);
      break;

      default: // Normal animation loop
      bitmapColumnIndex = 0;
      break;
    }

    if(stripingAdvance == -3) {
      stripingIndex = random(stripingCount);
      stripingModulo = stripingCount; // move to mode selection
    }
    else {
      stripingIndex += stripingAdvance;
    }
    if(stripingIndex >= stripingCount) {
      stripingIndex = 0;
    }

    if(++animationLoopCounter >= 64) animationLoopCounter = 0;
    if(animationLoopCounter % 16 == 0) newAnimationParameters();

    // Rotate active modules faster when less modules are active
    if(activeModuleCount == 1 || random8((80-darkSlider)/3) == 0) {
      moduleActivePattern = leftBitRotate(moduleActivePattern, 1);
      eraseInactiveModules();
    }

    // Select new active module pattern - higher dark value has faster module change
    if(animationLoopCounter == 0) { //|| random8(5 -((darkSlider % 32) >> 3)) == 0) {
      activeModuleIndex = random8(sizeof(modulePatterns));
      moduleActivePattern = modulePatterns[activeModuleIndex];
      activeModuleCount = bitSum(moduleActivePattern);
      eraseInactiveModules();
  }
}

void eraseInactiveModules() {
    #ifdef MAX_PARALLEL_MODULES
      // Black out inactive modules after switching active module list
      for(uint8_t m=0; m < MODULE_COUNT; m++) {
        // Bitshift 8-bit active module pattern from left side to know if module is active or not
        if(!(moduleActivePattern & moduleBitmask >> m) && lightSlider < 24) {
          fill_solid(leds[m], LEDS_PER_MODULE, CRGB::Black);
          FastLED[m].showLeds();
        }
      } 
    #endif    
}

void newAnimationParameters() {

    // Automatic attenuation of macro controls
    darkSlider = darkSlider >= 1 ? darkSlider-1 : 0;
    lightSlider = lightSlider >= 2 ? lightSlider-2 : 0;

    if(animationLoopCounter == 0) {
      // MEW MODE
      //animationIndex++;
      animationIndex = random(bitmapCount);
      if(animationIndex > bitmapCount-1) animationIndex = 0;
      prevPixelBaseAddr = pixelBaseAddr;
      pixelBaseAddr = pixelData[animationIndex];

      // To loop animation every beat:
      // (132bpm / 60 sec per min) * 1000ms = number of millis each loop should be
      // numMills / bitmapWidth (24pixels) = animationFPS
      animationRateDivisor = 8 / (random(2) + 1) / (random(2) + 1) / (random(2) + 1);
    }

    backgroundHue = random8();

    // Skip a random number of striping segments, with a good chance most or all are on
    stripingModulo = random(stripingCount) + 1;
    stripingModulo = stripingModulo > stripingCount / 2 ? stripingModulo / 2 : stripingModulo;

    stripingAdvance = random(4); // -3 is random striping segment, good chance of +/-1
    if(stripingAdvance == 0 || stripingAdvance == stripingModulo) stripingAdvance++; //no change = boring

    smoothingMode = random(9);

    smoothingRate = random(4) + 1; // higher is smoother

    /* prevent animations from resetting at random positions for now
    animationResetMode = random(4); */
    animationResetRange = random(6) + 2;

    mirrorOddStripingAnimation = random(2) == 1 ? true : false;

    // lower number, better chance of advancing, >12 all-strobe
    strobeModuleAdvance = random(16) + 1;;

    tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
    tft.setTextSize(1);

    // Update touchscreen display
    tft.setCursor(56, 302);
    if(animationIndex < 10) tft.print(0);
    tft.print(animationIndex);

    // Update stats text line
    tft.setCursor(2, 160);
    tft.print(F(" smooth="));
    tft.print(smoothingMode);
    tft.print(F(" modulePattern="));
    tft.print(moduleActivePattern, BIN);
        
    tft.print(F("      "));


}

void drawColorSelector() {
  // Make the color selection boxes
  for(byte i=0; i < colorSelectorCount; i++) {
      tft.fillRect(i*BOXSIZE, 0, BOXSIZE, BOXSIZE, colorSelector[i]);
      // Highlight selected color with cyan border
      if(currentDrawColor[0] == colorSelector[i]) {
        tft.drawRect(i*BOXSIZE, 0, BOXSIZE, BOXSIZE, ILI9341_CYAN);
      }
  }

  // Dark / light sliders
  tft.setCursor(14, 190);
  tft.setTextColor(ILI9341_WHITE);  tft.setTextSize(1);
  tft.print(F("Dark"));
  tft.drawRect(10, 200, SLIDERSIZE, SLIDERSIZE*2+2, ILI9341_RED);

  tft.setCursor(52, 190);
  tft.setTextColor(ILI9341_WHITE);  tft.setTextSize(1);
  tft.print(F("Light"));
  tft.drawRect(50, 200, SLIDERSIZE, SLIDERSIZE*2+2, ILI9341_RED);

  tft.setCursor(96, 190);
  tft.setTextColor(ILI9341_WHITE);  tft.setTextSize(1);
  tft.print(F("Bright"));
  tft.drawRect(90, 200, SLIDERSIZE, SLIDERSIZE*2+2, ILI9341_RED);

  // BPM tap sensor display
  tft.drawRect(115 + BOXSIZE*2, 200, BOXSIZE*2, BOXSIZE*2, ILI9341_RED);
  tft.setCursor(195, 220);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);  tft.setTextSize(1);
  tft.print(F("BPM"));
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK); tft.setTextSize(2);
  tft.setCursor(187, 230);
  if (bpmAverage<100) tft.print(0);
  tft.print(bpmAverage);   

  // Drawing options
  tft.setCursor(12, 280);
  tft.setTextColor(ILI9341_WHITE);  tft.setTextSize(1);
  tft.print(F("Paint"));
  tft.drawRect(10, 290, BOXSIZE+2, BOXSIZE, ILI9341_RED); 
  tft.drawLine(11, 291, 10+BOXSIZE, 288+BOXSIZE, ILI9341_WHITE);  
  tft.drawLine(11, 288+BOXSIZE, 10+BOXSIZE, 291, ILI9341_WHITE);  
  tft.setCursor(18+BOXSIZE, 280);
  tft.setTextColor(ILI9341_WHITE);  tft.setTextSize(1);
  tft.print(F("Bitmap"));
  tft.drawRect(15+BOXSIZE, 290, BOXSIZE+2, BOXSIZE, ILI9341_RED);     
}

void updateTouchScreen() {
  if (ts.touched()) {
    touchMillis = millis();  
    TS_Point p = ts.getPoint();

    // Scale from touchscreen coordinates to lcd using the calibration #'s
    p.x = map(p.x, TS_MINX, TS_MAXX, 0, tft.width());
    p.y = map(p.y, TS_MINY, TS_MAXY, 0, tft.height());

    // If touching the color selector area
    if (p.y < BOXSIZE) {
      currentDrawColor[0] = colorSelector[p.x / BOXSIZE];
      drawColorSelector();
    }

    // If touching the slider area
    if(p.y > 200 && p.y < 266) {
      if(p.x > 10 && p.x < 10+SLIDERSIZE) {
          darkSlider = p.y - 201;
          tft.fillRect(11, 201+darkSlider, SLIDERSIZE-2, 265-201-darkSlider, ILI9341_BLACK);
          tft.fillRect(11, 201, SLIDERSIZE-2, darkSlider, ILI9341_WHITE);
      }
      if(p.x > 50 && p.x < 50+SLIDERSIZE) {
          lightSlider = p.y - 201;
          tft.fillRect(51, 201+lightSlider, SLIDERSIZE-2, 265-201-lightSlider, ILI9341_BLACK);
          tft.fillRect(51, 201, SLIDERSIZE-2, lightSlider, ILI9341_WHITE);
      }
    }

    // Paint touch point on lcd
    //if (((p.y-PENRADIUS) > BOXSIZE) && ((p.y+PENRADIUS) < tft.height())) {
    //  tft.fillCircle(p.x, p.y, PENRADIUS, currentDrawColor[0]);
    //}

    // If touching bitmap animation area
    if (p.y > BOXSIZE+DIVIDERSIZE && p.y < (bitmapHeight * BITMAPSCALE) + BOXSIZE + DIVIDERSIZE) {

      // Paint touch point on lcd
      tft.fillCircle(p.x, p.y, PENRADIUS, currentDrawColor[0]);

      p.x = map(p.x, 0, tft.width(), 0, bitmapWidth);
      p.y = map(p.y, BOXSIZE+DIVIDERSIZE, tft.height() - BOXSIZE-DIVIDERSIZE-DIVIDERSIZE, 0, bitmapHeight*2);

      // Write touch data to in-memory bitmap animation
      paintedBitmap[(p.x * bitmapHeight) + p.y] = currentDrawColor[0];

      // Fade paintbrush to black over time
      currentDrawColor[0] = fadeToBlack(currentDrawColor[0]);
    }

    // If clear painted button touched
    if(p.y > 290 && p.y < 290 + BOXSIZE && p.x > 10 && p.x < 10+BOXSIZE) {
      memset(paintedBitmap,0,sizeof(paintedBitmap));
    }

    // If clear bitmap button touched
    if(p.y > 290 && p.y < 290 + BOXSIZE && p.x > 15+BOXSIZE && p.x < 15+BOXSIZE*2) {
      uint32_t tapInterval = touchMillis - prevTouchMillis;
      
      if(tapInterval > 500) {
        currentDrawMode = !currentDrawMode;
        prevTouchMillis = touchMillis;
      }
    }    
    
    // If bpm button touched
    if(p.y > 115 + BOXSIZE*2 && p.x > 200 && p.y < 115 + BOXSIZE*4 && p.x < 200 + BOXSIZE*2) {    
      uint32_t tapInterval = touchMillis - prevTouchMillis ;

      if(tapInterval > 400) { // tap < 150bpm in ms
        if(tapInterval < 750) { // tap > 80bpm in ms

          bpmAverage = ((60000L / tapInterval) + bpmAverage) / 2; // running average

          animationLoopRate = (uint32_t)(bpmAverage*1000ul) / 60 / bitmapWidth;
          animationLoopRate /= animationRateDivisor;

          tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK); tft.setTextSize(2);
          tft.setCursor(187, 230);
          if (bpmAverage<100) tft.print(0);
          tft.print(bpmAverage); 
        }

        prevTouchMillis = touchMillis;
      }
    }
  }
}

void updateEncoders() {
  // Read click button states
  if(digitalRead(lightKnobButton) == LOW && digitalRead(darkKnobButton) == LOW) {
    if(currentMillis - pauseTime > 500) {
      pauseTime = currentMillis;
      pauseState = !pauseState;
    }
  }

  darkKnobPrev = darkKnobPosition;
  lightKnobPrev = lightKnobPosition;

  darkKnobPosition = darkKnob.read();
  lightKnobPosition = lightKnob.read();

  // Set slider position based on relative change to decouple knob position from automatic slider attenuation
  if(digitalRead(darkKnobButton) == LOW) { // if knob depressed

  } else {
    darkSlider = darkSlider + (darkKnobPosition - darkKnobPrev);
    if(darkSlider > 200) darkSlider = 0;
    if(darkSlider > 64) darkSlider = 64;
    tft.fillRect(11, 201+darkSlider, SLIDERSIZE-2, 265-201-darkSlider, ILI9341_BLACK);
    tft.fillRect(11, 201, SLIDERSIZE-2, darkSlider, ILI9341_WHITE);
  }

  if(digitalRead(lightKnobButton) == LOW) { // if knob depressed
    brightSlider = (brightSlider + (lightKnobPosition - lightKnobPrev));
    if(brightSlider > 200) brightSlider = 0;
    if(brightSlider > 64) brightSlider = 64;
    tft.fillRect(91, 201+brightSlider, SLIDERSIZE-2, 265-201-brightSlider, ILI9341_BLACK);
    tft.fillRect(91, 201, SLIDERSIZE-2, brightSlider, ILI9341_WHITE);
  } else {
    lightSlider = (lightSlider + (lightKnobPosition -lightKnobPrev));
    if(lightSlider > 200) lightSlider = 0;
    if(lightSlider > 64) lightSlider = 64;
    tft.fillRect(51, 201+lightSlider, SLIDERSIZE-2, 265-201-lightSlider, ILI9341_BLACK);
    tft.fillRect(51, 201, SLIDERSIZE-2, lightSlider, ILI9341_WHITE);
  }
}

// Bit rotation within byte
uint8_t leftBitRotate(uint8_t n, uint8_t d) 
{ 
      
    /* In n<<d, last d bits are 0. To 
     put first 3 bits of n at  
    last, do bitwise or of n<<d  
    with n >>(INT_BITS - d) */
    return (n << d)|(n >> (MODULE_COUNT - d)); 
} 
  
/*Function to right rotate n by d bits*/
uint8_t rightBitRotate(uint8_t n, uint8_t d) 
{ 
    /* In n>>d, first d bits are 0.  
    To put last 3 bits of at  
    first, do bitwise or of n>>d 
    with n <<(INT_BITS - d) */
    return (n >> d)|(n << (MODULE_COUNT - d)); 
} 

// Calculate number of active bits in a byte
uint8_t bitSum(uint8_t n) {
  uint8_t sum = 0;
  for(uint8_t i=0; i < 8; i++) {
    sum += (n >> i) & 1;
  }
  return sum;
}

// Fade paintbrush color to black over time
uint16_t fadeToBlack(uint16_t color) {    
  // Unpack RGB565 16-bit color 
  uint8_t red = (color & 0XF800) >> 11;
  uint8_t green = (color & 0x7E0) >> 5;
  uint8_t blue = color & 0x1F;

  red -= red >= 2 ? 2 : red;
  green -= green >= 4 ? 4 : green;
  blue -= blue >= 2 ? 2 : blue;

  return (red << 11) | (green << 5) | blue;
}