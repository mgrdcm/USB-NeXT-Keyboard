// NeXT non-ADB Keyboard to USB converter
// This will take an older NeXT keyboard, talk to it, and turn the keycodes into a USB keyboard
// Requires an Arduino Micro for the USB portion - but could be ported to another micro fairly easily
// Written by Limor Fried / Adafruit Industries
// Released under BSD license - thanks NetBSD! :)
//
// Timing reference thanks to http://m0115.web.fc2.com/
// Pinouts thanks to http://www.68k.org/~degs/nextkeyboard.html
// Keycodes from http://ftp.netbsd.org/pub/NetBSD/NetBSD-release-6/src/sys/arch/next68k/dev/

#include "wsksymdef.h"
#include "nextkeyboard.h"

// the timing per bit, 50microseconds
#define TIMING 50

// pick which pins you want to use
#define KEYBOARDOUT 3
#define KEYBOARDIN 2

// comment to speed things up, uncomment for help!
//#define DEBUG 

// speed up reads by caching the 'raw' pin ports
volatile uint8_t *misoportreg;
uint8_t misopin;
// our little macro
#define readkbd() ((*misoportreg) & misopin)

// debugging/activity LED
#define LED 13

#define NEXT_KMBUS_IDLE 0x200600

// NeXT Keyboard Defines
// modifiers
#define NEXT_KB_CONTROL 0x1000
#define NEXT_KB_ALTERNATE_LEFT 0x20000
#define NEXT_KB_ALTERNATE_RIGHT 0x40000
#define NEXT_KB_COMMAND_LEFT 0x8000
#define NEXT_KB_COMMAND_RIGHT 0x10000
#define NEXT_KB_SHIFT_LEFT 0x2000
#define NEXT_KB_SHIFT_RIGHT 0x4000

#define KMBUS_SET(signal)  digitalWrite(KEYBOARDOUT, signal);
#define KMBUS_HOLD(signal, duration)  digitalWrite(KEYBOARDOUT, signal); delayMicroseconds(TIMING * duration);

// special command for setting LEDs
void setLEDs(bool leftLED, bool rightLED) {
  // |_________---_??_______-|
  KMBUS_HOLD(LOW, 9);
  KMBUS_HOLD(HIGH, 3);
  KMBUS_HOLD(LOW, 1);
  
  KMBUS_HOLD( (leftLED ? HIGH : LOW), 1);
  KMBUS_HOLD( (rightLED ? HIGH : LOW), 1);

  KMBUS_HOLD(LOW, 7);
  KMBUS_SET(HIGH);
}

// tell keyboard to send key state
void queryKeyboard() {
  // |_____-___-|
  KMBUS_HOLD(LOW, 5);
  KMBUS_HOLD(HIGH, 1);
  KMBUS_HOLD(LOW, 3);
  KMBUS_SET(HIGH);
}

// tell keyboard to send mouse state
void queryMouse() {
  // |_-___-___-|
  KMBUS_HOLD(LOW, 1);
  KMBUS_HOLD(HIGH, 1);
  KMBUS_HOLD(LOW, 3);
  KMBUS_HOLD(HIGH, 1);
  KMBUS_HOLD(LOW, 3);
  KMBUS_SET(HIGH);
}

// reset the bus
void nextreset() {
  // |_----_------__________-|
  KMBUS_HOLD(LOW, 1);
  KMBUS_HOLD(HIGH, 4);
  KMBUS_HOLD(LOW, 1);
  KMBUS_HOLD(HIGH, 6);
  KMBUS_HOLD(LOW, 10);
  KMBUS_SET(HIGH);
}


void setup() {
  // set up pin directions
  pinMode(KEYBOARDOUT, OUTPUT);
  pinMode(KEYBOARDIN, INPUT);
  pinMode(LED, OUTPUT);
  
  misoportreg = portInputRegister(digitalPinToPort(KEYBOARDIN));
  misopin = digitalPinToBitMask(KEYBOARDIN);
  
  // according to http://cfile7.uf.tistory.com/image/14448E464F410BF22380BB
  queryKeyboard();
  delay(5);
  nextreset();
  delay(8);
  
  queryKeyboard();
  delay(5);
  nextreset();
  delay(8);
  
  Keyboard.begin();
  Mouse.begin();

#ifdef DEBUG
  while (!Serial)
  Serial.begin(57600);
  Serial.println("NeXT");
#endif
}

uint32_t getresponse() {
  // bitbang timing, read 22 bits 50 microseconds apart
  cli();
  while ( readkbd() );
  delayMicroseconds(TIMING/2);
  uint32_t data = 0;
  for (uint8_t i=0; i < 22; i++) {
      if (readkbd())
        data |= ((uint32_t)1 << i);
      delayMicroseconds(TIMING);
  }
  sei();
  return data;
}

void loop() {
  digitalWrite(LED, LOW);
  delay(20);
  uint32_t resp, respMouse;
  queryKeyboard();
  resp = getresponse();
  queryMouse();
  respMouse = getresponse();
  
  // check for mouse input
  if (respMouse != NEXT_KMBUS_IDLE) {

#ifdef DEBUG
    Serial.print("MOUSE: "); Serial.print(respMouse, HEX); Serial.print(" / "); Serial.println(respMouse, BIN);
#endif

    // buttons
    if (respMouse & 0x1000)
      Mouse.release(MOUSE_RIGHT);
    else
      Mouse.press(MOUSE_RIGHT);
    
    if (respMouse & 0x2)
      Mouse.release(MOUSE_LEFT);
    else
      Mouse.press(MOUSE_LEFT);

    // movement
    int moveX = 0,
        moveY = 0;

    // check for vertical movement
    if (respMouse & 0x2000) {
      // moved vertically, now which direction?
      if (respMouse & 0xFC000)
        moveY = 10;
      else
        moveY = -10;
    }
    
    // check for horizontal movement
    if (respMouse & 0x04) {
      // moved horizontally, now which direction?
      if (respMouse & 0x1F8)
        moveX = 10;
      else
        moveX = -10;
    }

    Mouse.move(moveX, moveY, 0);
  }

  // check for a 'idle' response, we'll do nothing
  if (resp == NEXT_KMBUS_IDLE) return;
  
  // turn on the LED when we get real resposes!
  digitalWrite(LED, HIGH);

  // keycode is the lower 7 bits
  uint8_t keycode = resp & 0xFF;
  keycode /= 2;
  
#ifdef DEBUG
  Serial.print('['); Serial.print(resp, HEX);  Serial.print("] ");
  Serial.print("keycode: "); Serial.print(keycode);
#endif

  // modifiers! you can remap these here, 
  // but I suggest doing it in the OS instead
  if (resp & NEXT_KB_CONTROL)
    Keyboard.press(KEY_LEFT_CTRL);
  else 
    Keyboard.release(KEY_LEFT_CTRL);

  if (resp & NEXT_KB_SHIFT_LEFT) {
    Keyboard.press(KEY_LEFT_SHIFT);
  } else { 
    Keyboard.release(KEY_LEFT_SHIFT);
  }
  if (resp & NEXT_KB_SHIFT_RIGHT) {
    Keyboard.press(KEY_RIGHT_SHIFT);
  } else {
    Keyboard.release(KEY_RIGHT_SHIFT);
  }
  boolean shiftPressed = (resp & (NEXT_KB_SHIFT_LEFT|NEXT_KB_SHIFT_RIGHT)) != 0;
  
  // turn on shift LEDs if shift is held down
  if (shiftPressed)
    setLEDs(true, true);
  else
    setLEDs(false, false);
    
  if (resp & NEXT_KB_COMMAND_LEFT)
    Keyboard.press(KEY_LEFT_GUI);
  else 
    Keyboard.release(KEY_LEFT_GUI);
    
  if (resp & NEXT_KB_COMMAND_RIGHT)
    Keyboard.press(KEY_RIGHT_GUI);
  else 
    Keyboard.release(KEY_RIGHT_GUI);

  if (resp & NEXT_KB_ALTERNATE_LEFT)
    Keyboard.press(KEY_LEFT_ALT);
  else 
    Keyboard.release(KEY_LEFT_ALT);
  if (resp & NEXT_KB_ALTERNATE_RIGHT)
    Keyboard.press(KEY_RIGHT_ALT);
  else 
    Keyboard.release(KEY_RIGHT_ALT);

  if (keycode == 0) return;
  
  for (int i = 0; i< 100; i++) {
    if (nextkbd_keydesc_us[i*3] == keycode) {
      keysym_t keydesc = nextkbd_keydesc_us[i*3+1];
      char ascii = (char) keydesc;

#ifdef DEBUG
      Serial.print("--> ");      Serial.print(ascii); Serial.print(" / "); Serial.print(keydesc, HEX);
#endif

      int code;
      switch (keydesc) {
        case KS_KP_Enter:
        case KS_Return:    code = KEY_RETURN; break;
        case KS_Escape:    code = KEY_ESC; break;
        case KS_BackSpace: code = KEY_BACKSPACE; break;
        case KS_Up:        code = KEY_UP_ARROW; break;
        case KS_Down:      code = KEY_DOWN_ARROW; break;
        case KS_Left:      code = KEY_LEFT_ARROW; break;
        case KS_Right:     code = KEY_RIGHT_ARROW; break;

        // hacks for two tricky numpad keys
        case KS_KP_Equal:  code = (shiftPressed ? KS_bar : ascii); break;
        case KS_KP_Divide:
          if (shiftPressed) {
            Keyboard.release(KEY_RIGHT_SHIFT);
            Keyboard.release(KEY_LEFT_SHIFT);

            code = KS_backslash;            
          } else {
            code = ascii;
          }
          break;
        
        // remap the other special keys because the KeyboardMouse can't send proper vol/brightness anyway
        case KS_AudioLower:  code = KEY_INSERT; break;
        case KS_AudioRaise:  code = KEY_DELETE; break;
        case KS_Cmd_BrightnessUp:    code = KEY_PAGE_UP; break;
        case KS_Cmd_BrightnessDown:  code = KEY_PAGE_DOWN; break;
        
        case 0:
        default: code = ascii;
      }
      if ((resp & 0xF00) == 0x400) {  // down press
#ifdef DEBUG
        Serial.println(" v ");
#endif
        Keyboard.press(code);
        break;
      }
      if ((resp & 0xF00) == 0x500) {
        Keyboard.release(code);
#ifdef DEBUG
        Serial.println(" ^ ");
#endif
        break;
      }
      
      // re-press shift if need be
      if (keydesc == KS_KP_Divide && shiftPressed) {
          if (resp & NEXT_KB_SHIFT_LEFT)
            Keyboard.press(KEY_LEFT_SHIFT);
          if (resp & NEXT_KB_SHIFT_RIGHT)
            Keyboard.press(KEY_RIGHT_SHIFT);
      }
    }
  }
}

