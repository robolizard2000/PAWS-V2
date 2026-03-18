// https://lastminuteengineers.com/ds3231-rtc-arduino-tutorial/
// https://github.com/enjoyneering/AHTxx/blob/main/examples/AHT20_Serial/AHT20_Serial.ino
// https://lastminuteengineers.com/rotary-encoder-arduino-tutorial/


//#include <Wire.h>           // Residual code used in testing
#include <LiquidCrystal_I2C.h>// This is used for easy comunication with the LCD
#include <AHTxx.h>            // This is for comunication for the tempriture and humidity sensor

//#include <Arduino.h> // Residual code used in testing 
#include "RTClib.h"    // This to reseave the current time from the RTC

#include <Servo.h> // this is to handle the PWM control of the servos 

// Pin Setting
#define CLK 6  // First Input from rotary encoder
#define DT 5   // Second Input from rotary encoder
#define SW 7   // The Pin for the button on the rotary encoder

#define Door 4  // Door Input
#define LED 12  // The Light Output

#define Heat 8  // Heat flag for the temp control board
#define Cool 9  // Cool flag for the temp control board

struct Set{ // Creates a structure for a setting
  public:   // Sets the public data
    char* Name;   // The Name pf the setting
    float Value;  // The Current value 
    float Defult; // The Defult value for the setting
    float Step;   // The size of the steps that will be used for adjustment
    float Min;    // The Mininmum allowable  value
    float Max;    // The Maximum allowable  value 

    //Sets up the constructor for the setting structure 
    Set(char name[10], float value, float defult, float step, float min, float max){
      Name = name;
      Value = value;
      Defult = defult;
      Step = step;
      Min = min;
      Max = max;
    }
};

class ServoD{ // This is the class that handles the servos 
  private: // Sets the varibles that cant be seen externaly
    Servo Internal_Servo;
    uint8_t Open; // Open position for the servo
    uint8_t Close;// Closed position for the servo
  public:
    bool State; // Single public varible to allow the code to see what state the servo is in
    
    // This is the constructor of the servo class 
    ServoD(uint8_t open, uint8_t close, bool state){
      Open = open;
      Close = close;
      State = state;      
    }

    // This runs the attach servo to the pin and sets the defult state
    void ATTACH(int pin){  // Takes an int input to set the pin of the servo
      Internal_Servo.attach(pin);
      delay(50); // Delay to allow the servo to be setup 
      Internal_Servo.write(Close);
      delay(50); // Delay for the servo to move
    }
    void Gate_Open(){ // This will open the servo
      State = true;
      Internal_Servo.write(Open);
      delay(50);
    }
    void Gate_Close(){ // This will open the servo
      State = false;
      Internal_Servo.write(Close);
      delay(50);
    }
};

LiquidCrystal_I2C LCD(0x27, 20, 4);           // Creates the I2C LCD Object With (Address=0x27, Cols=20, Rows=4) 
                                              // NOTE: The address had to be change to reflect the replacement screen
AHTxx aht20(AHTXX_ADDRESS_X38, AHT2x_SENSOR); // Creates the object that handles the temp/humid sensor
RTC_DS3231 rtc;                               // Creates the Real Time Clock object

// List of chariter arrays to store the names of the Menues
char Main_Menu[5][12] = {"Run Control", "Settings", "Device Info", "Idle Stats","Servo Test"}; 
Set Settings[8] = { // Holds the stettings data
  // Name of setting , Current Value , Defult Value, Step, Min, Max
  Set("Temp Min", 25.0, 25.0, 0.25, 5.0, 80.0), // 0
  Set("Temp Max", 30.0, 30.0, 0.25, 5.0, 80.0), // 1
  Set("Slack T", 2.0, 2.0, 0.05, 0.0, 20.0),    // 2
  Set("Humid Min", 50.0, 50.0, 0.5, 10.0, 90.0),// 3
  Set("Humid Max", 60.0, 60.0, 0.5, 10.0, 90.0),// 4
  Set("Slack H", 2.0, 2.0, 0.05, 0.0, 20.0),    // 5
  Set("Debug", 0.0, 0.0, 1.0, 0.0, 1.0),        // 6
  Set("Exit", 0.0, 0.0, 0.0, 0.0, 0.0)          // 7 
};

// Sets up the Servos with the open and closed positions and what the Default position
ServoD Humidifyer  (5,180,false); 
ServoD Dehumidifyer(5,180,false);
//Servo H;

bool heat = false; // Heating Flag 
bool cool = false; // Cooling Flag

// Vars to store The Current Temp/humidity
float Real_Temp; 
float Real_Humid;
float ahtValue; // This is just a temperary holding var

// Varible for the rotary encoder to work
int counter = 0; // Used for the Test
int currentStateCLK;
int lastStateCLK;
String currentDir = ""; // Used for the Test

char Real_Time[10]; // Text for the current time
char Real_Day[10];  // Text for the current Date

int ERRORS = 0b00000000; // Varible to store the error codes

void setup(){
  // Runs the function to attach the servos to pins
  Humidifyer.ATTACH(10);
  Dehumidifyer.ATTACH(11);

  // Encoder Pin Set up
  pinMode(CLK, INPUT);
  pinMode(DT, INPUT);
  pinMode(SW, INPUT_PULLUP);
  lastStateCLK = digitalRead(CLK);

  // GPIO set up
  pinMode(Door, INPUT);
  pinMode(LED, OUTPUT);
  pinMode(Heat, OUTPUT);
  pinMode(Cool, OUTPUT);

  // LCD Set up
  LCD.init();
  LCD.backlight();
  LCD.setCursor(0, 0);  

  // Temp/Humidaty set up
  while (aht20.begin() != true){ // Runs if the AHT20 doesnt boot and conect
    LCD.print("AHT2x not connected or fail to load calibration coefficient");
    ERRORS = ERRORS || 0b00000001; // Sets an error flag
    delay(5000); // Waits a little while untill it attempts agian
  }
  // Desplays that the AHt20 has conected fine
  LCD.clear();
  LCD.setCursor(0, 0); 
  LCD.print("AHT20 OK");

  // RTC Setup
  if (!rtc.begin()) { // Runs if the RTC doesnt connect
    LCD.clear();
    LCD.setCursor(0, 0); 
    LCD.print("Couldn't find RTC");
    ERRORS = ERRORS || 0b00001000; // Sets an error flag
  }
  if (rtc.lostPower()) { // This runs if the RTC losses it battery or if a battery isnt pressent
    LCD.clear();
    LCD.setCursor(0, 0); 
    LCD.print("RTC lost power, setting time!");
    ERRORS = ERRORS || 0b00010000; // Sets an error flag
    // Set to compile time
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // Will reset the set time to the previous compile time
  }
}

void Update_Stats(){ // Function that will poll the AHT25 for both temp and humidity
  ahtValue = aht20.readTemperature(); //read 6-bytes via I2C, takes 80 milliseconds
  if (ahtValue != AHTXX_ERROR) {Real_Temp = ahtValue;} // If no error then stores value
  else{ // Runs code if the sensor errors
    LCD.clear();
    LCD.setCursor(0, 0);
    LCD.print("AHT_ERROR_TEMP");
    ERRORS = ERRORS || 0b00000010; // Sets an error flag
    delay(1000);
  }
  ahtValue = aht20.readHumidity(); //read 6-bytes via I2C, takes 80 milliseconds
  if (ahtValue != AHTXX_ERROR) Real_Humid = ahtValue;  // If no error then stores value
  else{ // Runs code if the sensor errors
    LCD.clear();
    LCD.setCursor(0, 0);
    LCD.print("AHT_ERROR_HUMID");
    ERRORS = ERRORS || 0b00000100; // Sets an error flag
    delay(1000);
  }
}

void Update_Time(){ // This updates the date and time varibles 
  DateTime now = rtc.now();
  // Stores the time into the varible with text formatting 
  sprintf(Real_Time, "%d:%d:%d", 
                     now.hour(), now.minute(), now.second());
  // Stores the Date into the varible with text formatting 
  sprintf(Real_Day, "%d/%d/%d", 
                     now.day(), now.month(), now.year());
}

void Rotary_Test() { // Page to test the gates / Servos 
  LCD.clear();
  LCD.setCursor(0, 0);
  LCD.print("____SERVO__TEST_____"); // Title
  delay(1000);
  lastStateCLK = digitalRead(CLK); // Sets current state
  bool temp = true; 
  while (temp){ // Loop for the page to poll the the rotary encoder for both button and turning
    currentStateCLK = digitalRead(CLK); // Reads the clock signal
    if (currentStateCLK != lastStateCLK && currentStateCLK == 1) { // Runs if the clock signal changes 
      if (digitalRead(DT) != currentStateCLK) { // If there is a differants betten the two then it will run
        counter--;// Varible to store a count being reduced by one
        currentDir = "CCW";
        Humidifyer.Gate_Close(); // Runs the Close for both gates
        Dehumidifyer.Gate_Close();
      } 
      else {
        counter++;// Varible to store a count being increased by one
        currentDir = "CW";
        Humidifyer.Gate_Open(); // Runs the Open for both gates
        Dehumidifyer.Gate_Open();
      }
      LCD.setCursor(0, 2);
      LCD.print(currentDir); // Displays the direction
      LCD.setCursor(0, 3);
      LCD.print(counter); // Current counter
    }
    lastStateCLK = currentStateCLK;
    int btnState = digitalRead(SW); // polls the button 
    if (btnState == LOW) { 
      temp = false;
    }
  }
}

void Menu_Select(){ // The main loop that is the home menu 
  uint8_t Selection = 0;    // The apsolute cursor position in the menu 
  int8_t Selection_L = -1;  // Last position
  uint8_t Local_Sel = 0;    // Local position on the LCD of the cursor
  uint8_t Desplay_top = 0;  // The index of the top most position desplayed 
  uint8_t Length = sizeof(Main_Menu) / sizeof(Main_Menu[0]); // Gets the length of the menu
  bool Run = true;
  lastStateCLK = digitalRead(CLK);

  LCD.clear();
  while(Run){
    //Desplay The menue
    if (Selection !=Selection_L){ // Checks if the the selection has changed
      LCD.clear();
      for (int i = 0; i<4; i++){ // will run through the 4 menu items to be ddesplayed 
        LCD.setCursor(1, (i));
        LCD.print(Main_Menu[i + Desplay_top]);
      }
      LCD.setCursor(0, Local_Sel); // Goes to the Current local menu selection to draw cursor
      LCD.print(">"); 
    }

    // Selection
    Selection_L = Selection; // Updates the current selection
    if (Settings[6].Value == 1.0){ // Only will run if the Debug mode is enabled 
      LCD.setCursor(19, 3);        // Desplays the current selection 
      LCD.print(Selection);
    }
    // This will first check for a change then check the direction
    currentStateCLK = digitalRead(CLK);
    if (currentStateCLK != lastStateCLK && currentStateCLK == 1) {
      if (digitalRead(DT) != currentStateCLK){ 
        if(Selection>0) {                     // Ckecks for selection to be above 0 to stop the selection to go negative 
          Selection--;                        
          if (Local_Sel == 0) {Desplay_top--;}// This will make page scroll up
          if (Local_Sel >  0) {Local_Sel  --;}// Scroll the cursor up
        } 
      }
      else {
        if(Selection<(Length-1)) { // This checks if the selection is at the bottom
          Selection++;
          if (Local_Sel == 3) {Desplay_top++;} // Scrolls down
          if (Local_Sel <  3) {Local_Sel  ++;} // Scrolls cursor down  
        }
      }
    }
    
    // Button Press
    lastStateCLK = currentStateCLK;
    int btnState = digitalRead(SW);
    if (btnState == LOW) {Run = false;}
  }
  delay(250);
  LCD.clear();
  switch(Selection){    // Uses a switch statment to select the page function to run
    case(0):
      Control_Screen();
      break;
    case(1):
      Settings_menu();
      break;
    case(2):
      Info_Screen();
      break;
    case(3):
      Idle_Screen();
      break;
    case(4):
      Rotary_Test();
      break;
  }
}

void Setting_Change(uint8_t Selection){ // This take the index of the setting that the user has selected to edit
  bool Changing = true;       // This is the main loop varible 
  bool value_changed = true;  // This is to reduce the amout of times the screen will refresh
  float change_hold = Settings[Selection].Value;
  LCD.clear(); // reset the LCD 
  LCD.setCursor(0,0);
  LCD.print("Setting: "); // Shows the name
  LCD.print(Settings[Selection].Name);
  LCD.setCursor(0,1);
  LCD.print("Has Default: "); // Shows the  defult value 
  LCD.print(Settings[Selection].Defult);
  LCD.setCursor(0,2);
  LCD.print("Currently: "); // Shows the value before any changes 
  LCD.print(Settings[Selection].Value);
  do{
    if (value_changed){// will only run if the vlaue has changed 
      LCD.setCursor(0,3);
      LCD.println("Ajustment: "); // Shows the current value it will be changed to
      LCD.setCursor(11,3); // Print line was used to clear the value on the line so the position needs to be set
      LCD.print(change_hold);
      value_changed = false;
    }
    // Checks to see if the user ajusts the values
    currentStateCLK = digitalRead(CLK);
    if (currentStateCLK != lastStateCLK && currentStateCLK == 1) {
      if (digitalRead(DT) != currentStateCLK) { 
        if (change_hold != Settings[Selection].Min){ // This will stop the user from changing the value over the max
          change_hold -= Settings[Selection].Step;
        }
      }
      else{ 
        if (change_hold != Settings[Selection].Max){ // This will stop the user from changing the value under the min
          change_hold += Settings[Selection].Step;
        }
      }
      value_changed = true; // This sets the flag to show the screen needs to be udated
    }
    lastStateCLK = currentStateCLK;
    int btnState = digitalRead(SW);
    if (btnState == LOW) {
      Changing = false;
      Settings[Selection].Value = change_hold; // Moves the value over to the memory 
    }
  }while (Changing);
  delay(250);
}


void Settings_menu(){ // The Screen for the menu of Settings 
  // most of this is the same as the main menu just with a differant list
  uint8_t Selection = 0;    // The current index of the menu
  int8_t Selection_L = -1;  // the previous index
  uint8_t Local_Sel = 0;    // The local index of the cursor on the LCD
  uint8_t Desplay_top = 0;  // The top index to be shown on the LCD
  uint8_t Length = sizeof(Settings) / sizeof(Settings[0]); // Length if the Settings list
  bool Run = true; // Main loop varible
  bool Value_Changed = false;
  lastStateCLK = digitalRead(CLK);

  LCD.clear();  
  while(Run){
    if (Selection !=Selection_L || Value_Changed == true){  // Checks for the Selection change 
      Value_Changed = false;                                // NOTE: the Value_Changed was added to fix a bug where afther exiting the setting
      LCD.clear();                                          //       change screen it wouldnt update ad desplay the menu
      for (int j = 0; j<4; j++){ // Desplays the current section of the menu
        LCD.setCursor(1, (j));
        LCD.print(Settings[j + Desplay_top].Name); // As Settings is an array of Set's the .Name is added to desplay the name of the setting
      }
      LCD.setCursor(0, Local_Sel); // Draws the cursor at the local index
      LCD.print(">"); 
    }

    // Selection
    Selection_L = Selection; // Updates the current index
    if (Settings[6].Value == 1.0){ // Only will run if the Debug mode is enabled 
      LCD.setCursor(19, 3);
      LCD.print(Selection);
    }
    
    currentStateCLK = digitalRead(CLK); 
    if (currentStateCLK != lastStateCLK && currentStateCLK == 1) { // Checks for movment 
      if (digitalRead(DT) != currentStateCLK){  // Checks direction of the movement
        if(Selection>0) {
          Selection--;
          if (Local_Sel == 0) {Desplay_top--;} // Scroles the menu up
          if (Local_Sel >  0) {Local_Sel  --;} // Moves the cursor up
        } 
      }
      else {
        if(Selection<(Length-1)) {
          Selection++;
          if (Local_Sel == 3) {Desplay_top++;}  // Scroles the menu Down
          if (Local_Sel <  3) {Local_Sel  ++;}  // Moves the cursor Down 
        }
      }
    }
    
    // Button Press
    lastStateCLK = currentStateCLK;
    int btnState = digitalRead(SW); // Polls for the button 
    if (btnState == LOW) { // Checks for the state of the button
      if(Selection < (Length-1)){ // any setting will be accepts other than the exit setting
        delay(250);
        Setting_Change(Selection); // Runs the function to change the setting of what is currently selected
        Value_Changed = true;
      }
      else{ Run = false;}// catches the exit case and will exit from the loop
    }
  }
  delay(250);
}

void Control_Screen(){ // The Screen for the Main controll loop
  bool Run = true; // Main lopp varible
  LCD.clear();
  LCD.setCursor(0, 0);
  LCD.print(F("____RUN__CONTROL____")); // The Title for the screen
  while(Run){
    Update_Stats(); // This runs the function to update the Temp / Humid 
    LCD.setCursor(0, 1);
    LCD.print(Real_Temp); LCD.print(F("*C       ")); // Prints the stats to the LCD
    LCD.print(Real_Humid); LCD.print(F("%"));
    Update_Time(); // Updated the time from the RTC 
    LCD.setCursor(10, 2);
    LCD.print(Real_Day); // Desplays the date and time
    LCD.setCursor(0, 2);
    LCD.print(Real_Time);

    if(digitalRead(Door)==HIGH){  // The controll logic is only ran if the Door is shut
      digitalWrite(LED, LOW);     // turns off the LED
      if (Settings[6].Value == 1.0){ // Only will run if the Debug mode is enabled 
        LCD.setCursor(0, 3);         // Desplays what is happerning on Screen 
        LCD.print("           Door Shut"); 
        LCD.setCursor(0, 3);
        if (digitalRead(Heat) == HIGH) {LCD.print("He ");} // These will show what the controller is trying to do
        if (digitalRead(Cool) == HIGH) {LCD.print("Co ");} // behide the debug settings as it should be set and forget
        if (Humidifyer.State == true)  {LCD.print("Hu ");}
        if (Dehumidifyer.State == true){LCD.print("De ");}
      }
      // Checks to see if heat is needed
      if(Real_Temp < Settings[0].Value){ 
        digitalWrite(Heat, HIGH); 
        heat = true; // setting the flag
      }
      else if ((heat == true) && (Real_Temp > Settings[0].Value + Settings[2].Value)){ // this is the hysterises for truning heat off
        digitalWrite(Heat, LOW); 
        heat = false; // setting the flag
      }

      // Checks to see if cooling is needed
      if(Real_Temp > Settings[1].Value){
        digitalWrite(Cool, HIGH); 
        cool = true;
      }
      else if ((cool == true) && (Real_Temp < Settings[1].Value - Settings[2].Value)){ // this is the hysterises for truning the cooling off
        digitalWrite(Cool, LOW); 
        cool = false;
      }
      
      // Checks the humidity to see if it is too low
      if(Real_Humid < Settings[3].Value) { Humidifyer.Gate_Open();}
      else if ((Humidifyer.State == true) && (Real_Humid > Settings[3].Value + Settings[5].Value)) {
        Humidifyer.Gate_Close();
      }

      // Checks the humidity to see if it is too high
      if(Real_Humid > Settings[4].Value) { Dehumidifyer.Gate_Open();}
      else if ((Dehumidifyer.State == true) && (Real_Humid < Settings[4].Value - Settings[5].Value)) {
        Dehumidifyer.Gate_Close();
      }
    }
    else{ // This will Run if the dorr is open 
      digitalWrite(LED, HIGH); // Turns the light On

      if (Settings[6].Value == 1.0){ // Desplay the debug message
        LCD.setCursor(0, 3);
        LCD.print("           Door Open");
      }

      // Turns off heating/cooling and closes both gates to save energy 
      digitalWrite(Cool, LOW); 
      cool = false;
      digitalWrite(Heat, LOW); 
      heat = false;
      Humidifyer.Gate_Close();
      Dehumidifyer.Gate_Close();
    }

    int btnState = digitalRead(SW); // polls for the buttin press
    if (btnState == LOW) {
      // Leaves the system in a safe state when exiting
      digitalWrite(Cool, LOW); 
      cool = false;
      digitalWrite(Heat, LOW); 
      heat = false;
      Humidifyer.Gate_Close();
      Dehumidifyer.Gate_Close();
      Run = false; // breaks out of the main loop
    }
    delay(250);
  }
}

void Idle_Screen(){ // Just a monitering Screen.
  bool Run = true;  // same as the Controll screen but without any controlling logic
  LCD.clear();
  LCD.setCursor(0, 0);
  LCD.print(F("_____IDLE_STATS_____"));
  while(Run){
    Update_Stats(); // Updates the temp and humidity varibles
    LCD.setCursor(0, 1);
    LCD.print(Real_Temp); LCD.print(F("*C       ")); // desplays these
    LCD.print(Real_Humid); LCD.print(F("%"));
    Update_Time(); // Updates the date and time from the RTC
    LCD.setCursor(10, 3);
    LCD.print(Real_Day); // Desplayes these
    LCD.setCursor(0, 3);
    LCD.print(Real_Time);
    
    //lastStateCLK = currentStateCLK;
    int btnState = digitalRead(SW);
    if (btnState == LOW) {Run = false;} // Runs if the buttin has been pressed for longer than 50ms
    delay(250);
  }
}

void Info_Screen(){ // Just an information screen currnetly just version number with error codes if in debug mode
  bool Run = true;
  LCD.clear();
  LCD.setCursor(0, 0);
  LCD.print("________INFO________"); // Main title 
  LCD.setCursor(0, 1);
  LCD.print("Version:      0.1.20"); // Version number. First in major version number. second is revision number and the last is quick fix number
  
  while(Run){
    if (Settings[6].Value == 1.0){ // Only will run if the Debug mode is enabled 
      LCD.setCursor(0, 3);
      LCD.print("Error Code: ");
      LCD.setCursor(12, 3);
      LCD.print(ERRORS); // Shows the error codes set by flags 
    }

    int btnState = digitalRead(SW);
    if (btnState == LOW) { Run = false;} // Runs if the buttin has been pressed for longer than 50ms
    delay(250);
  }
}

void loop() {
  Menu_Select(); // Moves loop to an funtion for easier modificatin in future
}
