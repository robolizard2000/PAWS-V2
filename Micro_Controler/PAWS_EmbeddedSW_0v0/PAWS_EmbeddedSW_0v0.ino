//Pin definitions


//Settings
int measurementClk = 10; //time in seconds for how frequently sensors are read and data is read from BLE

int backlightSleepTMR; //time in minutes before screen times out and back light turns off
bool LCDLock = False; //if set to true locks out the LCD and rotary encoder 

bool debugEN = False; //if set to true then it will expose the debug settings


//Control variables------------------
int CountCLKmS = 0; //timing count in mS
float CountCLKS = 0.0; //timing count in S
int runTime = 0; 
int prevrunTime = 0;

float tempSetPoint;
float tempHysterises;

float humidSetPoint;
float humidHysterises;

int windMax;

int lightMin;
bool lightOveride = False;
bool lightSet = False;

bool windowOveride = False;
bool windowSet = False;

bool relay1Set = False; //control of heater
bool relay2Set = False; //control of cooler

//Sensors + data variables
float tempDegC;
float tempDegCavg;

int humidPercent;

int windSpeedRPM; //using measurement of PWM period to determine RPM
float windSpeedMPH; //using calculation 1.4MPH = 1RPS

int rainDigital;

int lightLevel;



void setup() {
  

}

void loop() {

    prevrunTime = runTime;
    runTime = Millis();
    CountCLKmS = CountCLKmS + (runTime - prevrunTime);
    CountCLKS = CountCLKmS/1000; 

    //displayUpdate(); //function including control for updating values on display and control of display menu system
  if(CountCLKS >= measurementClk){  
    //readSensors(); //function to read sensors and 

    //readBLE(); //function to call BLE read services from app ie read setting changes

    //writeBLE(); //function to call BLE transmit services to send settings, sensor data and output changes

    //autoControl(); //function for checking sensor data against threshold values for automated control conditions and actioning them if met
  }