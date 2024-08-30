
//Board to choose: ESP32 Arduino -> ESP32 Dev Module
//Works!

//Upgrades:
//Voltage Resolution increased to 12 bits
//Integral over defined interval displayed on built-in LCD screen
//////////////////////////////////////////////////////

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

/*
 * Global Variables
*/
uint8_t int_ch_init=52; //Initial limit for integral calculation
uint8_t int_ch_end=80; //End limit for integral calculation


uint16_t data[SPEC_CHANNELS];

boolean laser_state, led_state;

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST); //TFT display object
int start_time=0; //Start time to measure the display_time
int display_time=0; //Variable to measure the display refresh time
unsigned long MIN_integral=700; //Minimum value for the bar indicator
unsigned long MAX_integral=4095; //Maximum value for the bar indicator
unsigned long fluo_limit=1160; //Limit to be greater than 6 GPT
int coefficient=3;
int slope=819;
int divider=584;

void setup(){

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
  tft.setTextColor(ST77XX_GREEN);
  tft.print("Total:");
  tft.setTextColor(ST77XX_WHITE);
  
  //Serial initialization
  Serial.begin(115200); // Baud Rate set to 115200
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
  for(int i = 0; i < 15; i++){

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
    
    Serial.print(data[i]);
    Serial.print(',');
    
  }
  
  Serial.print("\n");
}

/*
 This function integrates (sum) the spectrum over an specified interval
*/
unsigned long integrate(unsigned int chstart, unsigned int chstop){
  unsigned long sum=0;
  for(int i=chstart; i < chstop; i++){
    sum=sum+data[i];
    return sum;
  }
}

void loop(){
  if(Serial.available()>0){
    char caracter=Serial.read();
    if(caracter=='U'){
      laser_state=not laser_state;
      digitalWrite(LASER_404,laser_state);
    }else if(caracter=='L'){
      led_state=not led_state;
      digitalWrite(WHITE_LED,led_state);
    }else if(caracter=='i'){
      int_ch_init=Serial.parseInt(); //integral initial limit
    }else if(caracter=='f'){
      int_ch_end=Serial.parseInt(); //integral final limit
    }
  }
  readSpectrometer();
  printData();
  delay(10);
  display_time=millis()-start_time;
  if(display_time>1000){ //Update display each second
    start_time=millis();
    unsigned long integral=integrate(int_ch_init,int_ch_end);
    float result=(coefficient*integral+slope)/divider;//Result in the GPT scale
    String string_result;
    if(integral<fluo_limit){
      string_result="<= 6.0 GPT";
    } else{
      string_result="   "+String(result,1)+" GPT";
    }
    tft.fillRect(25, 40, 200, 25, ST77XX_BLACK);
    tft.setCursor(25, 40);
    tft.print(string_result);
    //Here it goes the bar indictor
    int barWidth = map(integral, MIN_integral, MAX_integral, 0, 200);
    tft.fillRect(25, 80, 200, 5, ST77XX_BLACK); //Clear previous indicator
    tft.fillRect(25, 80, barWidth, 5, ST77XX_GREEN); //Bar indicator
    //For debugging:
    tft.setTextSize(1);
    tft.fillRect(25,120,120, 15, ST77XX_BLACK);
    tft.setCursor(25, 120);
    tft.print(integral);
    tft.setTextSize(3);
    //End debugging
  }
}
