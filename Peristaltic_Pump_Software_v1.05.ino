// Version 1.05: gemeinsame Software fuer Hardware v1.01 und v1.02.
// Korrektur: Tastendruck bei Fertig bestaetigt nur, ohne Neustart.
// Enthalten: sichere LCD-Zeilen und Restmengenanzeige aus Software v1.03.
// Restmenge ist aus Motorschritten berechnet, keine Durchflussmessung.
// LCD wird hoechstens alle 500 ms und nur bei geaenderten Zeichen aktualisiert.
// Dabei entstehen kurze zusaetzliche Pausen zwischen den Motorschritten.
// HARDWARE-AUSWAHL: Nur HARDWARE_VERSION auf 1 oder 2 setzen und neu hochladen.
// 1 = Aufbau aus Software v1.01: LCD parallel, USB/Serial 9600 Baud.
// 2 = Aufbau aus Software v1.02: LCD per I2C, USB/Serial 57600 Baud.
// Die Boardauswahl in der Arduino IDE weiterhin passend zum echten Board setzen.
// Der Schalter waehlt Verdrahtung und LCD-Bibliothek, nicht das Arduino-Board.
#ifndef HARDWARE_VERSION
#define HARDWARE_VERSION 1 // 1 fuer deine derzeit verwendete Hardware; 2 fuer I2C.
#endif

#define LCD_COLUMNS 16 // Beide vorhandenen Aufbauten verwenden 16x2 Zeichen.
#define LCD_ROWS 2
#define LCD_I2C_ADDRESS 0x27 // Nur fuer Hardware 2; Adresse aus Software v1.02.

// Gemeinsame Bibliotheken fuer beide Aufbauten.
#include <Arduino.h> // Arduino-Typen und Standardfunktionen.
#include <ClickEncoder.h> // Drehgeber mit Timer-Abfrage.
#include <TimerOne.h> // Timer fuer ClickEncoder.
#include <stdio.h> // snprintf fuer begrenzte Textausgabe.
#include <string.h> // strlen und memcpy.
#include <EEPROM.h> // Einstellungen mit unveraendertem Speicherlayout.

// Nur die Bibliothek fuer den ausgewaehlten Aufbau muss installiert sein.
#if HARDWARE_VERSION == 1
  #include <LiquidCrystal.h> // Parallel angeschlossenes LCD aus Software v1.01.
  #define BAUD 9600 // Bisherige USB-Kommunikationsrate der Hardware 1.
  #define LCD_PIN_RS 13 // Register Select.
  #define LCD_PIN_EN 12 // Enable.
  #define LCD_PIN_D4 8 // LCD-Datenleitungen im 4-Bit-Modus.
  #define LCD_PIN_D5 9
  #define LCD_PIN_D6 10
  #define LCD_PIN_D7 11
  LiquidCrystal lcd(LCD_PIN_RS, LCD_PIN_EN, LCD_PIN_D4, LCD_PIN_D5,
                    LCD_PIN_D6, LCD_PIN_D7);
#elif HARDWARE_VERSION == 2
  #include <Wire.h> // Auf Uno/Nano: SDA = A4, SCL = A5.
  #include <LiquidCrystal_I2C.h> // Gleiche LCD-Bibliothek wie Software v1.02.
  #define BAUD 57600 // Bisherige USB-Kommunikationsrate der Hardware 2.
  LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, LCD_COLUMNS, LCD_ROWS);
#else
  #error "HARDWARE_VERSION muss 1 (Parallel-LCD) oder 2 (I2C-LCD) sein."
#endif

//ENCODER --------------------------------------------------------------------------------
#define ENCODER_PIN_BUTTON 2
#define ENCODER_PIN_A 3
#define ENCODER_PIN_B 4
ClickEncoder *encoder;
int16_t last, value;

//STEP MOTOR -----------------------------------------------------------------------------
#define MOTOR_STEP_PIN 7
#define MOTOR_DIR_PIN 6
#define STEP_MODE 4 // (1: Full Step, 2: Half Step, 4: Quarter Step, ...)
#define STEPS_PER_FULL_ROT 200 // @ full steps
long delay_us;
long steps;
long step_counter = 0;

//CALIBRATION -----------------------------------------------------------------------------
#define CALIBR_ROTATIONS 30
#define CALIBR_DURATION 30 // seconds
#define CALIBR_DECIMALS 3
const int CALIBR_DECIMAL_CORR = 1000; // 10^3, passend zu CALIBR_DECIMALS

//SERIAL COMMUNICATION ---------------------------------------------------------------------
// BAUD wird oben passend zur Hardware ausgewaehlt.
String inputString = "";         // a String to hold incoming data
boolean stringComplete = false;  // whether the string is complete
long vol_uL=0;
long rate_uL_min =0;
int cal=0;
boolean usb_start=0;
char inChar;


//STATE ------------------------------------------------------------------------------------
boolean in_menu=0;
volatile boolean in_action=0;
boolean menu_entered=0;
boolean menu_left=0;

//GENERAL -----------------------------------------------------------------------------------
#define MICROSEC_PER_SEC 1000000

const unsigned int CALIBR_STEPS = CALIBR_ROTATIONS * STEPS_PER_FULL_ROT * STEP_MODE;
const unsigned int CALIBR_DELAY_US = (CALIBR_DURATION * MICROSEC_PER_SEC)/(CALIBR_STEPS*2);


//MENU ---------------------------------------------------------------------------------------
#define MAX_NUM_OF_OPTIONS 4
#define NUM_OF_MENU_ITEMS 10
#define VALUE_MAX_DIGITS 4
int menu_number_1=0;
int menu_number_2=1;
boolean val_change =0;
double value_dbl;
// dtostrf-Breite ist eine Mindestbreite, keine Begrenzung!
// Z.B. 999.9 braucht mit Abschlusszeichen 6 Bytes statt bisher nur 5.
char value_str[16];
char lcd_previous[LCD_ROWS][LCD_COLUMNS + 1] = {{0}};
unsigned long progress_updated_ms = 0;
boolean dose_finished_screen = false;

// Explizite Deklarationen fuer Arduino und eigenstaendige Syntaxpruefung.
void write_lcd_row(byte row, const char *text);
void print_menu_row(byte row, int item);
void update_dose_progress(boolean force);
void sync_units();
void motor_wait(long microseconds);

enum menu_type {
  VALUE,
  OPTION,
  ACTION
};

typedef struct 
{
  const char* name_;
  menu_type type; //0: value type, 1:option type, 2:action type
  int value;
  int decimals;
  int lim;
  const char* options[4];
  const char* suffix;
}menu_item;
int menu_items_limit = 10-1;
menu_item menu[10];


//███ SETUP ████████████████████████████████████████████████████████████████████████████████████████████████████
void setup(){
  
 pinMode(MOTOR_STEP_PIN,OUTPUT); 
 pinMode(MOTOR_DIR_PIN,OUTPUT);
 digitalWrite(MOTOR_DIR_PIN,LOW);
 digitalWrite(MOTOR_STEP_PIN,LOW);

 menu[0].name_ = "Start";
 menu[0].type = ACTION;
 menu[0].value = 0;
 menu[0].lim = 0;
 menu[0].suffix = "RUN!";

 menu[1].name_ = "Vol";
 menu[1].type = VALUE;
 menu[1].value = 0;
 menu[1].decimals = 1;
 menu[1].lim = 9999;
 menu[1].suffix="mL";

 menu[2].name_ = "V.Einh:";
 menu[2].type = OPTION;
 menu[2].value = 0;
 menu[2].lim = 3-1;
 menu[2].options[0] = "mL";
 menu[2].options[1] = "uL";
 menu[2].options[2] = "rot";

 menu[3].name_ = "S"; // Kurzer Name: Wert und mL/min passen in 16 Zeichen.
 menu[3].type = VALUE;
 menu[3].value = 0;
 menu[3].decimals = 1;
 menu[3].lim = 9999;
 menu[3].suffix="mL/min";

 menu[4].name_ = "S.Einh:";
 menu[4].type = OPTION;
 menu[4].value = 0;
 menu[4].lim = 3-1;
 menu[4].options[0] = "mL/min";
 menu[4].options[1] = "uL/min";
 menu[4].options[2] = "rpm";

 menu[5].name_ = "Drehr:";
 menu[5].type = OPTION;
 menu[5].value = 0;
 menu[5].lim = 2-1;
 menu[5].options[0] = "Rechts";
 menu[5].options[1] = "Links";

 menu[6].name_ = "Mod:";
 menu[6].type = OPTION;
 menu[6].value = 0;
 menu[6].lim = 3-1;
 menu[6].options[0] = "Dosieren";
 menu[6].options[1] = "Pumpen";
 menu[6].options[2] = "Cal";

 menu[7].name_ = "Cal";
 menu[7].type = VALUE;
 menu[7].value = 0;
 menu[7].decimals = CALIBR_DECIMALS;
 menu[7].lim = 30000;
 menu[7].suffix="mL";

 menu[8].name_ = "Einst. speichern";
 menu[8].type = ACTION;
 menu[8].value = 0;
 menu[8].lim = 0;
 menu[8].suffix = "OK!";

 menu[9].name_ = "USB Ctrl";
 menu[9].type = ACTION;
 menu[9].value = 0;
 menu[9].lim = 0;
 menu[9].suffix = "EIN!";

  for (int i=0; i <= menu_items_limit; i++){
      // Ungueltige EEPROM-Werte duerfen nie als Optionsindex dienen.
      int saved = eepromReadInt(i*2);
      menu[i].value = (saved >= 0 && saved <= menu[i].lim) ? saved : 0;
  }
  
  encoder = new ClickEncoder(ENCODER_PIN_B, ENCODER_PIN_A, ENCODER_PIN_BUTTON, 4); //(Encoder A, Encoder B, PushButton)
  encoder->setAccelerationEnabled(false);
  Timer1.initialize(1000);
  Timer1.attachInterrupt(timerIsr);
  last = 0;
  
  Serial.begin(BAUD);
  inputString.reserve(200);

  // Unterschiedliche Initialisierung der beiden LCD-Bibliotheken.
#if HARDWARE_VERSION == 1
  lcd.begin(LCD_COLUMNS, LCD_ROWS); // Parallel-LCD, wie in Software v1.01.
#elif HARDWARE_VERSION == 2
  lcd.init(); // I2C-LCD, wie in Software v1.02.
#endif


  sync_units();
  // Auch die gespeicherte Drehrichtung beim Einschalten anwenden.
  digitalWrite(MOTOR_DIR_PIN, menu[5].value == 0 ? LOW : HIGH);
  update_lcd();
  steps = steps_calc(menu[1].value, menu[2].value, menu[7].value, menu[1].decimals);
  delay_us = delay_us_calc(menu[3].value, menu[4].value, menu[7].value, menu[3].decimals);
  
}



//███ LOOP ████████████████████████████████████████████████████████████████████████████████████████████████████

void loop() {
  
// BUTTON HANDLING ////////////////////////////////////////////////////////////////////////////////

ClickEncoder::Button b = encoder->getButton();
  if (b != ClickEncoder::Open) {
    switch (b) {
      case ClickEncoder::Clicked:
        // Fertig bestaetigen: diesen Klick verbrauchen, keine neue Dosis starten.
        if (dose_finished_screen) {
          in_action = false;
          in_menu = false;
          menu_entered = false;
          menu_left = false;
          menu_number_1 = 0; // Zur normalen Start-Menueanzeige zurueckkehren.
          menu_number_2 = 1;
          // Drehimpulse aus dem letzten Lauf nicht nachtraeglich auswerten.
          encoder->getValue();
          last = value;
          val_change = false;
          lcd.noBlink();
          update_lcd(); // Setzt auch dose_finished_screen auf false.
          break; // Erst der naechste Klick darf die Start-Aktion ausloesen.
        }
        if(menu[menu_number_1].type == VALUE ||menu[menu_number_1].type == OPTION){ // if value or option type
          in_menu =!in_menu;
        }
        if(menu[menu_number_1].type == ACTION){ // if action type
          in_action=!in_action;
          step_counter= 0;
        }
        
        if (in_action == true ||in_menu ==true){ //menu entered
          menu_entered = true;
        }
        
        if (in_action == false && in_menu ==false){ //menu left
          menu_left = true;
        }
        break;
        
      case ClickEncoder::DoubleClicked: {
          if (menu[menu_number_1].type == VALUE){
          // In long rechnen: z.B. Kalibrierung 30000 + 3000 passt nicht in AVR-int.
          long next = (long)menu[menu_number_1].value + menu[menu_number_1].lim/10;
          menu[menu_number_1].value = (next > menu[menu_number_1].lim)
                                     ? menu[menu_number_1].lim : (int)next;
          val_change=true;
          }
        break;
      }
      case ClickEncoder::Held:
          if (menu[menu_number_1].type == VALUE){
          menu[menu_number_1].value = 0;
          val_change=true;
          }
        break;
      case ClickEncoder::Released:
        break;
        }
  }


/// SETUP ////////////////////////////////////////////////////////////////////////////////

if (menu_entered){
  lcd.blink();
  if (menu[menu_number_1].type == ACTION){
    // Auch Aktionsbeschriftungen durch den gemeinsamen LCD-Puffer fuehren.
    char line[LCD_COLUMNS + 1];
    memcpy(line, lcd_previous[0], sizeof(line));
    const char *label = menu[menu_number_1].suffix;
    byte start = LCD_COLUMNS - strlen(label);
    memcpy(line + start, label, strlen(label));
    write_lcd_row(0, line);
    lcd.setCursor(15, 0);
  }
  if (menu[menu_number_1].type == VALUE){
    encoder->setAccelerationEnabled(true);
  }
  if (menu_number_1 == 0 && in_action && menu[6].value == 0) {
    lcd.noBlink();
    write_lcd_row(0, "0|Start     !RUN");
    update_dose_progress(true);
  }
  menu_entered = false;
}



/// ACTIONS ////////////////////////////////////////////////////////////////////////////////

if (in_action){
  switch (menu_number_1){
  case 0: //Start
  if (menu[6].value == 0){ //Dose
    // Nullwerte oder fehlende Kalibrierung duerfen keine Motorpulse erzeugen.
    if (steps <= 0 || delay_us <= 0) {
      exit_action_menu();
      write_lcd_row(0, "Werte pruefen");
      write_lcd_row(1, "Vol / S / Cal");
    } else if (dose(steps, delay_us, step_counter)) {
      // Abschluss bleibt sichtbar, bis der Knopf gedreht/gedrueckt wird.
      dose_finished_screen = true;
      in_action = false;
      lcd.noBlink();
      write_lcd_row(0, "Fertig");
      update_dose_progress(true);
    } else {
      update_dose_progress(false);
    }
  } else if (menu[6].value == 1){ //Pump
    pump(delay_us);
  } else if (menu[6].value == 2){ //Cal.
    if (dose(CALIBR_STEPS, CALIBR_DELAY_US, step_counter)){
      exit_action_menu();
    }
  }
  break;

  case 8:
   for (int i=0; i <= menu_items_limit; i++){
      eepromWriteInt(i*2,menu[i].value);
   }
   delay(700);
   menu_left = true;
  break;
  
  case 9:
  while (Serial.available()) {
    inChar = (char)Serial.read();     // get the new byte:
    step_counter = 0;
    if (inChar == 'p'){
      rate_uL_min=Serial.parseInt();
      cal=Serial.parseInt();
      if(cal==0){
        cal = menu[7].value;
      }
      delay_us = delay_us_calc(rate_uL_min, 1, cal, 0);
      usb_start=true;
    } else if (inChar == 'd'){
      vol_uL=Serial.parseInt();
      rate_uL_min=Serial.parseInt();
      cal=Serial.parseInt();
      if(cal==0){
        cal = menu[7].value;
      }
      steps = steps_calc(vol_uL, 1, cal, 0);
      delay_us = delay_us_calc(rate_uL_min, 1, cal, 0);
      usb_start=true;
    } else if (inChar == 'c'){
      usb_start=true;
    } else if (inChar == 'w'){
      cal=Serial.parseInt();
      menu[7].value =cal;
      for (int i=0; i <= menu_items_limit; i++){
      eepromWriteInt(i*2,menu[i].value);
      }
      usb_start=false;
    } else if (inChar == 'x'){
      usb_start=false;
    }
  }
  
  if (usb_start) {
    if(inChar == 'p'){
      pump(delay_us);
    } else if (inChar == 'd') {
      if (dose(steps, delay_us, step_counter)){
        usb_start = false;
      }
    } else if (inChar == 'c'){
      if (dose(CALIBR_STEPS, CALIBR_DELAY_US, step_counter)){
        usb_start = false;
      }
    }
  }
  break;
}

/// MENU (no action) ////////////////////////////////////////////////////////////////////////////////

} else if (!in_action){

if (val_change==true){
  update_lcd();
  val_change=false; // Zuweisung statt wirkungslosem Vergleich
}
  
value += encoder->getValue(); // encoder update

if (!in_menu){ // no menu selected
  val_change = encoder_selection(menu_number_1, menu_number_2, menu_items_limit); //process value change

}else if(in_menu){ // menu selected
  if(menu[menu_number_1].type == 0){
    val_change = encoder_value_selection(menu[menu_number_1].value, menu[menu_number_1].lim);
  } else {
    val_change = encoder_selection(menu[menu_number_1].value, menu[menu_number_1].lim);
  }
}

}

/// CLOSE ////////////////////////////////////////////////////////////////////////////////

if (menu_left){
  lcd.noBlink();
  if (menu[menu_number_1].type == ACTION){
    exit_action_menu();
  }
  if (menu[menu_number_1].type == VALUE){
    encoder->setAccelerationEnabled(false);
  }
  
  // Einheiten anhand des Optionsindex bestimmen, keine Zeiger vergleichen.
  sync_units();

  if (menu_number_1 == 5){ //Change Direction
    if (menu[5].value == 0){
      digitalWrite(MOTOR_DIR_PIN,LOW); 
    }else{
      digitalWrite(MOTOR_DIR_PIN,HIGH);
    }
  }
  steps = steps_calc(menu[1].value, menu[2].value, menu[7].value, menu[1].decimals);
  delay_us = delay_us_calc(menu[3].value, menu[4].value, menu[7].value, menu[3].decimals);
  
  update_lcd(); // Einheit und Dezimalstellen sofort gemeinsam erneuern.
  menu_left = false;
}

} 

//███ FUNCTION DECLARATION █████████████████████████████████████████████████████████████████████████████████████████████████

//_____________________________________________________________________________________________

void timerIsr() {
  encoder->service();
}

//_____________________________________________________________________________________________

boolean dose(long _steps, long _delay_us, long & inc) {
      if (_steps <= 0 || _delay_us <= 0) return true;
      if(inc < _steps){
        digitalWrite(MOTOR_STEP_PIN,HIGH); 
        motor_wait(_delay_us);
        digitalWrite(MOTOR_STEP_PIN,LOW); 
        motor_wait(_delay_us);
        inc++;
        return false;
      } else {
        // Zaehler bis zum naechsten Start erhalten: Restmenge bleibt null.
        return true;
      }
}
//_____________________________________________________________________________________________
/*
void dose_slow(long _steps, int _delay_us, long & inc) {
      if(inc < _steps){
        digitalWrite(MOTOR_STEP_PIN,HIGH); 
        delay(_delay_us/1000); 
        digitalWrite(MOTOR_STEP_PIN,LOW); 
        delay(_delay_us/1000);
        inc++;
      } else {
        inc=0;
        exit_action_menu();
      }
}*/
//_____________________________________________________________________________________________

void pump(long _delay_us) {
        if (_delay_us <= 0) return; // Keine Pulse bei ungueltiger Geschwindigkeit.
        digitalWrite(MOTOR_STEP_PIN,HIGH); 
        motor_wait(_delay_us); 
        digitalWrite(MOTOR_STEP_PIN,LOW); 
        motor_wait(_delay_us);
}
//_____________________________________________________________________________________________

void exit_action_menu(){
   in_action = false;
   usb_start = false;
   dose_finished_screen = false;
   lcd.noBlink();
   update_lcd(); // Keine Leerzeichen ueber das Zeilenende hinaus schreiben.
}
//_____________________________________________________________________________________________

long steps_calc(long volume, int unit_mode, int calibr, int decimals){
  if (volume <= 0 || unit_mode < 0 || unit_mode > 2 ||
      (unit_mode != 2 && calibr <= 0)) return 0; 
//unit_mode = menu[2].value, volume = menu[1].value, calib = mL/10rot
  
long _steps;
int decimal_corr;
double conv; // rotations/volume
double cal; //volume/rotation

decimal_corr = pow(10,decimals);
cal = calibr;
cal = (cal/CALIBR_ROTATIONS)/CALIBR_DECIMAL_CORR;

  if(unit_mode == 2){ //rot
    conv = 1.0;
  } else if (unit_mode == 1){ //uL
    conv = 1.0/cal/1000;
  } else if (unit_mode == 0){ //mL
    conv = 1.0/cal;
  }

  double calculated = STEPS_PER_FULL_ROT * STEP_MODE * conv * volume/decimal_corr;
  // Auf AVR ist long 32 Bit; 2147483647 wird in float bereits aufgerundet.
  if (calculated >= 2147483647.0 || calculated < 1.0) return 0;
  _steps = (long)calculated;
return _steps;
}
//_____________________________________________________________________________________________

long delay_us_calc(long vol_per_min, int unit_mode, int calibr, int decimals){
  if (vol_per_min <= 0 || unit_mode < 0 || unit_mode > 2 ||
      (unit_mode != 2 && calibr <= 0)) return 0;
  
double d_delay_us;
long _delay_us;
int decimal_corr;
double conv; // rotations/volume
double cal;  

decimal_corr = pow(10,decimals);

cal = calibr;
cal = (cal/CALIBR_ROTATIONS)/CALIBR_DECIMAL_CORR;

  if(unit_mode == 2){ //rot
    conv = 1.0;
  } else if (unit_mode == 1){ //uL
    conv = 1.0/cal/1000;
  } else if (unit_mode == 0){ //mL
    conv = 1.0/cal;
  }
  
  d_delay_us = (1/(STEPS_PER_FULL_ROT * STEP_MODE * conv * vol_per_min/decimal_corr))*60*MICROSEC_PER_SEC/2;
  if (d_delay_us >= 2147483647.0 || d_delay_us < 1.0) return 0;
  _delay_us = (long)d_delay_us;
  return _delay_us;
}

//_____________________________________________________________________________________________

// Die vorhandene Einheit und deren Nachkommastellen stets zusammen setzen.
void sync_units() {
  menu[1].suffix = menu[2].options[menu[2].value];
  menu[1].decimals = (menu[2].value == 1) ? 0 : 1;
  menu[3].suffix = menu[4].options[menu[4].value];
  menu[3].decimals = (menu[4].value == 1) ? 0 : 1;
}

// Lange Pausen ohne die bisherige int-Verkuerzung und ohne AVR-us-Ueberlauf.
void motor_wait(long microseconds) {
  if (microseconds > 16000) {
    delay((unsigned long)microseconds / 1000UL);
    unsigned int remainder = (unsigned long)microseconds % 1000UL;
    if (remainder > 0) delayMicroseconds(remainder);
  } else if (microseconds > 0) {
    delayMicroseconds((unsigned int)microseconds);
  }
}

// Genau 16 Zeichen, inklusive Auffuellen alter Zeichen mit Leerzeichen.
// Nur geaenderte Abschnitte schreiben: weniger I2C-Aufwand und kein lcd.clear().
void write_lcd_row(byte row, const char *text) {
  boolean contiguous = false;
  boolean ended = false;
  for (byte col = 0; col < LCD_COLUMNS; col++) {
    char ch = ' ';
    if (!ended) {
      ch = text[col];
      if (ch == '\0') { ended = true; ch = ' '; }
    }
    if (lcd_previous[row][col] != ch) {
      if (!contiguous) lcd.setCursor(col, row);
      lcd.print(ch);
      lcd_previous[row][col] = ch;
      contiguous = true;
    } else {
      contiguous = false;
    }
  }
  lcd_previous[row][LCD_COLUMNS] = '\0';
}

// Alle Menuezeilen sind begrenzt; Zahlenpuffer sind ausreichend gross.
void print_menu_row(byte row, int item) {
  char line[LCD_COLUMNS + 1];
  if (menu[item].type == VALUE) {
    double number = (double)menu[item].value / pow(10, menu[item].decimals);
    dtostrf(number, 1, menu[item].decimals, value_str);
    snprintf(line, sizeof(line), "%d|%s %s%s", item, menu[item].name_,
             value_str, menu[item].suffix);
  } else if (menu[item].type == OPTION) {
    snprintf(line, sizeof(line), "%d|%s %s", item, menu[item].name_,
             menu[item].options[menu[item].value]);
  } else {
    snprintf(line, sizeof(line), "%d|%s", item, menu[item].name_);
  }
  write_lcd_row(row, line);
}

void update_lcd(){
  dose_finished_screen = false;
  print_menu_row(0, menu_number_1);
  print_menu_row(1, menu_number_2);
  lcd.setCursor(1, 0);
}

// Anzeige aus tatsaechlich ausgegebenen Schrittimpulsen, nicht aus Laufzeit.
// Auf dem Display bezeichnet uL die Einheit Mikroliter (ASCII-kompatibel).
void update_dose_progress(boolean force) {
  unsigned long now = millis();
  if (!force && (unsigned long)(now - progress_updated_ms) < 500UL) return;
  progress_updated_ms = now;
  double remaining = 0.0;
  if (steps > 0 && step_counter < steps) {
    remaining = ((double)menu[1].value / pow(10, menu[1].decimals)) *
                ((double)(steps - step_counter) / (double)steps);
  }
  // Gleiche Einheit/Genauigkeit wie die eingestellte Wunschmenge.
  dtostrf(remaining, 1, menu[1].decimals, value_str);
  char line[LCD_COLUMNS + 1];
  snprintf(line, sizeof(line), "Rest: %s%s", value_str, menu[1].suffix);
  write_lcd_row(1, line);
}
//_____________________________________________________________________________________________

boolean encoder_selection(int & x, int lim){ //sub menu
  if (value > last) {
    x++;
    if(x>lim){
      x=0;
    }
    last = value;
    return true;
  }else if(value < last){
    x--;
    if(x<0){
      x=lim;
    }
    last = value;
    return true;
  } else {
    return false;
  }
}
//_____________________________________________________________________________________________

boolean encoder_value_selection(int & x, int lim){ //sub menu
  if (value != last) {
    // Beschleunigte Eingabe vor dem Zurueckwandeln in AVR-int begrenzen.
    long next = (long)x + (long)value - (long)last;
    x = (next > lim) ? 0 : ((next < 0) ? lim : (int)next);
    if(x>lim){
      x=0;
    }
    if(x<0){
      x=lim;
    }
    last = value;
    return true;
  } else {
    return false;
  }
}
//_____________________________________________________________________________________________

boolean encoder_selection(int & x, int & y, int lim){ //main menu
  if (value > last) {
    x++;
    y++;
    if(x>lim)
    {
      x=0;
    }
    if(y>lim)
    {
      y=0;
    }
    last = value;
    return true;
  }else if(value < last){
    y = menu_number_1;
    x--;
    if(x<0)
    {
      x=lim;
    }
    last = value;
    return true;
  }else {
    return false;
  }

}
//_____________________________________________________________________________________________

void eepromWriteInt(int adr, int wert) { 
//http://shelvin.de/eine-integer-zahl-in-das-arduiono-eeprom-schreiben/
byte low, high;

  low=wert&0xFF;
  high=(wert>>8)&0xFF;
  EEPROM.write(adr, low); // dauert 3,3ms 
  EEPROM.write(adr+1, high);
  return;
} //eepromWriteInt
//_____________________________________________________________________________________________

int eepromReadInt(int adr) { 
//http://shelvin.de/eine-integer-zahl-in-das-arduiono-eeprom-schreiben/
byte low, high;

  low=EEPROM.read(adr);
  high=EEPROM.read(adr+1);
  return low + ((high << 8)&0xFF00);
} //eepromReadInt
