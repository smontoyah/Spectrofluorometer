//Works perfectly from GPT 0 to GPT 10
//Board to choose: ESP32 Arduino -> ESP32 Dev Module
//Changes to this version:
//GPT claculation based on linear fit of calibration data.
//Calibration mode and spectrum mode.
//Calibration mode: prints the raw integral (get character C)
//Spectrum mode (default): prints the spectrum (get character S)
//Upgrades:
//Voltage Resolution increased to 12 bits
//Integral over defined interval displayed on built-in LCD screen
//////////////////////////////////////////////////////
//Bluetooth library
#include "BluetoothSerial.h"
BluetoothSerial SerialBT;
//Preferences library used to save and load the configuration of the fluorescence parameters
#include "Preferences.h";
Preferences config_fluorescence;

//Display libraries
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_ST7789.h> // Hardware-specific library for ST7789
#include <SPI.h>

// TFT display pins (pinouts from https://github.com/Xinyuan-LilyGO/TTGO-T-Display)
#define TFT_MOSI 19
#define TFT_SCLK 18
#define TFT_CS 5
#define TFT_DC 16
#define TFT_RST 23
#define TFT_BL 4

//Print mode flags
#define spectrometer true
#define calibration false
boolean print_mode=calibration;

/*
 * Spectrometer pins
 */
#define SPEC_TRG         39
#define SPEC_ST          32
#define SPEC_CLK         33
#define SPEC_VIDEO       25
#define WHITE_LED        26
#define LASER_404        27

#define SPEC_CHANNELS    288 // New Spec Channel

int delayTime;// (microseconds). The f(CLK) (Hz) will be: 1000000/(2*delayTime). 500 kHz if delayTime=1.

/*
 * Global Variables
*/

uint16_t data[SPEC_CHANNELS];
unsigned long INT_TIME; //Integration time in microseconds
unsigned long N_THP=30000; //Number of cycles for start pulse hifg period thp(ST) //Default = 15


boolean laser_state, led_state;

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST); //TFT display object
int start_time=0; //Start time to measure the display_time
int display_time=0; //Variable to measure the display refresh time

uint8_t int_ch_init=60; //52; //Initial limit for integral calculation
uint8_t int_ch_end=73; //80; //End limit for integral calculation
int MIN_average=700; //Minimum value for valid average data (otherwise it means the cuvette holder is empty)
int MAX_average=4095; //Maximum value for valid average data
int fluo_minimum=1450; //Maximum average value to calculate GPT
int fluo_maximum=1950; //Maximum average value to calculate GPT

//Fitting coeficients (GPT=slope*average+intercept)
float intercept=-12.552;//-20.812; //-27.69
float slope=0.0122;//0.0176; //0.0234

void setup(){
  //Storage space to save he configuration parameters
  config_fluorescence.begin("parameters", false); //False means read/write
  //Load parameter values from the storage space
  fluo_minimum=config_fluorescence.getInt("fluo_minimum", 1470); //Second argument is the default value, in case it doesnt find anything in the storage space.
  fluo_maximum=config_fluorescence.getInt("fluo_maximum", 1950);
  slope=config_fluorescence.getFloat("slope", 0.0122);
  intercept=config_fluorescence.getFloat("intercept", -12.552);
  
  SerialBT.print("Minimum raw data to print <4.5 GPT: ");
  SerialBT.println(fluo_minimum);
  SerialBT.print("Maximum raw data to print >=4.5 GPT: ");
  SerialBT.println(fluo_maximum);
  SerialBT.print("Fitting coefficents: GPT=slope*raw_integral+intercept:");
  SerialBT.print("Slope= ");
  SerialBT.println(slope);
  SerialBT.print("Intercept= ");
  SerialBT.println(intercept);
  //Spectrometer initialization
  pinMode(SPEC_CLK, OUTPUT);
  pinMode(SPEC_ST, OUTPUT);
  pinMode(LASER_404, OUTPUT);
  pinMode(WHITE_LED, OUTPUT);

  digitalWrite(SPEC_CLK, HIGH); // Set SPEC_CLK High
  digitalWrite(SPEC_ST, LOW); // Set SPEC_ST Low

  digitalWrite(LASER_404,0);

  //Display initialization
  pinMode(TFT_BL, OUTPUT);      // TTGO T-Display enable Backlight pin 4
  digitalWrite(TFT_BL, HIGH);   // T-Display turn on Backlight
  tft.init(135, 240);           // Initialize ST7789 240x135
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(3);
  tft.setRotation(1);
  tft.setCursor(25, 10);
  tft.setTextColor(ST77XX_BLACK);
  tft.print("TOTAL:");
  tft.setTextColor(ST77XX_WHITE);
  
  //Serial Bluetooth initialization
  SerialBT.begin("Fluorimeter");
  start_time=millis();
}

/*
 * This functions reads spectrometer data from SPEC_VIDEO
 * Look at the Timing Chart in the Datasheet for more info
 */
void readSpectrometer(){

  int delayTime = 1; // delay time

  // Start clock cycle and set start pulse to signal start
  digitalWrite(SPEC_CLK, LOW);
  delayMicroseconds(delayTime);
  digitalWrite(SPEC_CLK, HIGH);
  delayMicroseconds(delayTime);
  digitalWrite(SPEC_CLK, LOW);
  digitalWrite(SPEC_ST, HIGH);
  delayMicroseconds(delayTime);

  //Sample for a period of time
  for(int i = 0; i <  N_THP; i++){

      digitalWrite(SPEC_CLK, HIGH);
      delayMicroseconds(delayTime);
      digitalWrite(SPEC_CLK, LOW);
      delayMicroseconds(delayTime); 
 
  }

  //Set SPEC_ST to low
  digitalWrite(SPEC_ST, LOW);

  //Sample for a period of time
  for(int i = 0; i < 85; i++){

      digitalWrite(SPEC_CLK, HIGH);
      delayMicroseconds(delayTime);
      digitalWrite(SPEC_CLK, LOW);
      delayMicroseconds(delayTime); 
      
  }

  //One more clock pulse before the actual read
  digitalWrite(SPEC_CLK, HIGH);
  delayMicroseconds(delayTime);
  digitalWrite(SPEC_CLK, LOW);
  delayMicroseconds(delayTime);

  //Read from SPEC_VIDEO
  for(int i = 0; i < SPEC_CHANNELS; i++){

      data[i] = analogRead(SPEC_VIDEO);
      
      digitalWrite(SPEC_CLK, HIGH);
      delayMicroseconds(delayTime);
      digitalWrite(SPEC_CLK, LOW);
      delayMicroseconds(delayTime);
        
  }

  //Set SPEC_ST to high
  digitalWrite(SPEC_ST, HIGH);

  //Sample for a small amount of time
  for(int i = 0; i < 7; i++){
    
      digitalWrite(SPEC_CLK, HIGH);
      delayMicroseconds(delayTime);
      digitalWrite(SPEC_CLK, LOW);
      delayMicroseconds(delayTime);
    
  }

  digitalWrite(SPEC_CLK, HIGH);
  delayMicroseconds(delayTime);
  
}

/*
 * The function below prints out data to the terminal or 
 * processing plot
 */
void printData(){
  
  for (int i = 0; i < SPEC_CHANNELS; i++){
    
    SerialBT.print(data[i]);
    SerialBT.print(',');
    
  }
  
  SerialBT.print("\n");
}

/*
 This function integrates (sum) the spectrum over an specified interval
*/
unsigned long integrate(unsigned int chstart, unsigned int chstop){
  unsigned long sum=0;
  for(int i=chstart; i < chstop; i++){
    sum=sum+data[i];
  }
  return sum;
}

void loop(){
  if(SerialBT.available()>0){
    char caracter=SerialBT.read();
    if(caracter=='U'){
      laser_state=not laser_state;
      digitalWrite(LASER_404,laser_state);
    }else if(caracter=='L'){
      led_state=not led_state;
      digitalWrite(WHITE_LED,led_state);
    }else if(caracter=='I'){
      while(SerialBT.available()==0);
      INT_TIME=SerialBT.parseInt();//INT_TIME in microseconds
      if(INT_TIME>107&&INT_TIME<1000001){
       N_THP=long(INT_TIME/(delayTime*2.0))-48;//This calculation is based on the C12880MA datasheet.
      }
    }else if(caracter=='m'){
      while(SerialBT.available()==0);
      fluo_minimum=SerialBT.parseInt();
      config_fluorescence.putUInt("fluo_minimum", fluo_minimum); //Store value on the storage space
      SerialBT.print("Maximum raw data to print <=4.5 GPT: ");
      SerialBT.println(fluo_minimum);
    }else if(caracter=='M'){
      while(SerialBT.available()==0);
      fluo_maximum=SerialBT.parseInt();
      config_fluorescence.putUInt("fluo_maximum", fluo_maximum); //Store value on the storage space
      SerialBT.print("Maximum raw data to print >4.5 GPT: ");
      SerialBT.println(fluo_maximum);
    }else if(caracter=='s'){
      while(SerialBT.available()==0);
      slope=SerialBT.parseFloat();
      config_fluorescence.putFloat("slope", slope); //Store value on the storage space
    }else if(caracter=='i'){
      while(SerialBT.available()==0);
      intercept=SerialBT.parseFloat();
      config_fluorescence.putFloat("intercept", intercept); //Store value on the storage space
    }else if(caracter=='S'){
      print_mode=spectrometer;
    }else if(caracter=='C'){
      print_mode=calibration;
    }
  }
  readSpectrometer();
  delay(10);
  display_time=millis()-start_time;
  if(display_time>1000){ //Update display each second
      start_time=millis();
      unsigned long integral=integrate(int_ch_init,int_ch_end);
      float average=integral/(int_ch_end-int_ch_init);
      float result=(slope*average)+intercept;//Result in the GPT scale
      String string_result;
      if(average<MIN_average){
        string_result="VACIO!";
        tft.fillRect(25, 80, 200, 10, ST77XX_BLACK); //Clear previous indicator
      }else if(average<MAX_average){
        //Print the bar indicator
        int barWidth = map(average, MIN_average, MAX_average, 0, 200);
        tft.fillRect(25, 80, 200, 10, ST77XX_BLACK); //Clear previous indicator
        if(result<4){
          tft.fillRect(25, 80, barWidth, 10, ST77XX_RED); //Bar indicator
        }else{
          tft.fillRect(25, 80, barWidth, 10, ST77XX_GREEN); //Bar indicator
        }
        string_result="GPT: "+String(int(result));
      }else{
        string_result="OVL";
        tft.fillRect(25, 80, 200, 10, ST77XX_BLACK); //Clear previous indicator
      }
      
      //Print the string_result
      tft.fillRect(25, 40, 200, 25, ST77XX_BLACK);
      tft.setCursor(25, 40);
      tft.print(string_result);
          
      //Print the raw data (average)
      tft.setTextSize(1);
      tft.fillRect(25,120,120, 15, ST77XX_BLACK);
      tft.setCursor(25, 120);
      tft.print(average);
      tft.setTextSize(3);
      if(print_mode==calibration){
        SerialBT.println(average);
      }
  }
  if(print_mode==spectrometer){
    printData();
  }
}
