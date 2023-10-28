/*
 * Anotiste Moisture sensor application
 * - By Kaushik Nagabhushan
 */
#include <SoftwareSerial.h>
#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>

SoftwareSerial mySerial(7, 8); // RX, TX

#define BUFFER_SIZE 64
#define JSON_LENGTH 24
#define LOOP_COUNTER 900 //Each loop waits for 8s, so 900 times waiting = 7200s = 120 mins

typedef enum {
  RESP_OK,
  RESP_ERROR,
  RESP_TIMEOUT,
} RESP_CODE;

char idCharArray[24];
volatile int f_wdt=1;
unsigned short loopCount = 0;
const char *CMD_GET_ID = "AT+GSN";
boolean bFirstTime = true;


/***************************************************
 *  Name:        ISR(WDT_vect)
 *
 *  Returns:     Nothing.
 *
 *  Parameters:  None.
 *
 *  Description: Watchdog Interrupt Service. This
 *               is executed when watchdog timed out.
 *
 ***************************************************/
ISR(WDT_vect)
{
    f_wdt=1;
}

/***************************************************
 *  Name:        enterSleep
 *
 *  Returns:     Nothing.
 *
 *  Parameters:  None.
 *
 *  Description: Enters the arduino into sleep mode.
 *
 ***************************************************/
void enterSleep(void)
{
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);   /* EDIT: could also use SLEEP_MODE_PWR_DOWN for lowest power consumption. */
  sleep_enable();
  
  /* Now enter sleep mode. */
  sleep_mode();
  
  /* The program will continue from here after the WDT timeout*/
  sleep_disable(); /* First thing to do is disable sleep. */
  
  /* Re-enable the peripherals. */
  //power_all_enable(); -- Moved this to loop() to enable things just before we actually do anything
}

/***************************************************
 *  Name:        configureWatchDog
 *
 *  Returns:     Nothing.
 *
 *  Parameters:  None
 *
 *  Description: Sets up the watchdog to fire in 8s with no reset
 *
 ***************************************************/
 void configureWatchDog()
 {
  /*** Setup the WDT ***/
  
  /* Clear the reset flag. */
  MCUSR &= ~(1<<WDRF);
  
  /* In order to change WDE or the prescaler, we need to
   * set WDCE (This will allow updates for 4 clock cycles).
   */
  WDTCSR |= (1<<WDCE) | (1<<WDE);

  /* set new watchdog timeout prescaler value */
  WDTCSR = 1<<WDP0 | 1<<WDP3; /* 8.0 seconds */
  
  /* Enable the WD interrupt (note no reset). */
  WDTCSR |= _BV(WDIE);
  
 }

/***************************************************
 *  Name:        readSensorValue
 *
 *  Returns:     int - sensor value.
 *
 *  Parameters:  None
 *
 *  Description: Reads the moisture sensor value
 *
 ***************************************************/

int readSensorValue()
{  
  int analogSensorPin = 0;    // select the input pin for the potentiometer
  int analogSensorValue = 0;  // variable to store the value coming from the sensor
   
  int sensorVCC = 13;
  
   pinMode(sensorVCC, OUTPUT); 

  // power the sensor
  digitalWrite(sensorVCC, HIGH);
  delay(500); //make sure the sensor is powered
  // read the value from the sensor:
  analogSensorValue = analogRead(analogSensorPin);
  //stop power 
  digitalWrite(sensorVCC, LOW);  
  return analogSensorValue;
}

/***************************************************
 *  Name:        powerGSMModule
 *
 *  Returns:     Nothing
 *
 *  Parameters:  None
 *
 *  Description: Powers on/off the SIM900 GSM module
 *
 ***************************************************/

void powerGSMModule(boolean bEnable)
{
    char mesg[BUFFER_SIZE];
    memset(mesg, 0x00, BUFFER_SIZE);
    sprintf(mesg, "Powering %s SIM", bEnable?"ON":"OFF");
    Serial.println(mesg);
    
    pinMode(9, OUTPUT); 
    digitalWrite(9,LOW);
    delay(1000);
    digitalWrite(9,HIGH);
    delay(2000);
    digitalWrite(9,LOW);
    delay(3000);

    boolean bResponseDone = false;
    do {
      RESP_CODE errCode = RESP_ERROR;
      bResponseDone = waitForSequence(errCode);
      if (errCode != RESP_OK) {
        break;
      }
    } while(!bResponseDone);

    if (bEnable) {
      Serial.println("SIM900 is ON");
      boolean bReady = isSimReady();
      if (!bReady) {
        Serial.println("Failed to get ID");
      }
    }
}

/***************************************************
 *  Name:        waitForSequence
 *
 *  Returns:     True if the response is received fully
 *
 *  Parameters:  None
 *
 *  Description: Waits till a response has been received during startup or shutdown
 *
 ***************************************************/

boolean waitForSequence(RESP_CODE &errCode)
{
  String data;
  int timeToWait = 100;
  errCode = RESP_OK;
  
  while(timeToWait > 0 && mySerial.available()==0) {
    delay(100);
    timeToWait = timeToWait - 1;
  }

  if (timeToWait <= 0) {
    errCode = RESP_TIMEOUT;
    return false;
  }

  data = mySerial.readString();
  data.trim();
  
  Serial.println(data);
  delay(100);
  
  if (  -1 != data.indexOf("PSUTTZ") ||
        -1 != data.indexOf("NORMAL POWER DOWN")) {
    return true;
  }
  return false;
}

/***************************************************
 *  Name:        setup
 *
 *  Returns:     Nothing
 *
 *  Parameters:  None
 *
 *  Description: Powers on the SIM900, Configures the timer and initializes SIM900 HTTP
 *
 ***************************************************/

void setup() {
   
  // Open serial communications and wait for port to open:
  Serial.begin(19200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  memset(idCharArray, 0x00, 24);
  
  // set the data rate for the SoftwareSerial port
  mySerial.begin(19200);

  configureWatchDog();
}

/***************************************************
 *  Name:        loop
 *
 *  Returns:     Nothing
 *
 *  Parameters:  None
 *
 *  Description: 
 *
 ***************************************************/

void loop() { // run over and over
  
  if (!bFirstTime) {
    enterSleep();
  }
  
  loopCount = loopCount + 1;
  
  if (bFirstTime || loopCount >= LOOP_COUNTER) {
    //Re-enable the peripherals. They are disabled in the enterSleep() method
    power_all_enable();
    
    Serial.println("-- Running loop one more time --");
    delay(100);

    powerGSMModule(true);
    SetupHttp();
    SubmitPostRequest();
    CloseHttp();
    powerGSMModule(false);
    loopCount = 0;
    bFirstTime = false;
  }
}

/***************************************************
 *  Name:        isSimReady
 *
 *  Returns:     True if SIM900 is powered on and responding
 *
 *  Parameters:  None
 *
 *  Description: Checks if the SIM900 is responding to commands
 *
 ***************************************************/

boolean isSimReady()
{
  if (!sendCommand(CMD_GET_ID, false)) {
    //Try once more
    if (!sendCommand(CMD_GET_ID, false)) {
      return false;
    }
  }
  return true;
}

/***************************************************
 *  Name:        isResponseDone
 *
 *  Returns:     True if SIM900 response is complete
 *
 *  Parameters:  None
 *
 *  Description: Checks if the SIM900 response is complete
 *
 ***************************************************/

boolean isResponseDone(char *response)
{  
    if (  0 != strstr(response, "OK") || 
          0 != strstr(response, "ERROR") || 
          0 != strstr(response, "DOWNLOAD") ||
          0 != strstr(response, "CONNECT OK")) {
            return true;
          }

    //If it is not the standard responses, then we check for these special strings in the response
    //which might indicate that the response is actually done
        
    //The serial read buffer is only 64 bytes. For the AT+HTTPPARA command
    //the whole url is returned back. If this URL is more than 63 bytes, then the remaining characters
    //seem to be dropped. So to work around that, we check if the command is a HTTPPARA command,
    //and we go on with the process.
    if (  0 != (strstr(response, "URL")) ||
          0 != (strstr(response, "HTTPREAD:")) || 
          0 != (strstr(response, "Successfully "))
        ) {
      //We found a "URL" in the string, so continue saying that the response is done
      return true;
    }
    return false;
}

/***************************************************
 *  Name:        isHttpActionDone
 *
 *  Returns:     True if the response is a HTTP action
 *
 *  Parameters:  None
 *
 *  Description: If the HTTP action is a PUT or GET, and the response contains one
 *               of the response codes in the funtion, then return value is true.
 *
 ***************************************************/

boolean isHttpActionDone(char *response)
{          
    if (  0 != strstr(response, "HTTPACTION:1,601,0") ||
          0 != strstr(response, "HTTPACTION:1,200") ||
          0 != strstr(response, "HTTPACTION:1,202") ||
          0 != strstr(response, "HTTPACTION:0,200") ||
          0 != strstr(response, "HTTPACTION:0,202") ||
          0 != strstr(response, "HTTPACTION:0,601")) {
              return true;
          }
    return false;
}

/***************************************************
 *  Name:        waitForOk
 *
 *  Returns:     True if the command has been sent and the response is received fully
 *
 *  Parameters:  None
 *
 *  Description: Waits till a response has been received for a command
 *
 ***************************************************/

boolean waitForOk(const char *cmd, boolean isHttpAction, RESP_CODE &errCode)
{
  String data;
  char dataChar[BUFFER_SIZE];
  int timeToWait = 80;
  errCode = RESP_OK;
  memset(dataChar, 0x00, BUFFER_SIZE);

  while(timeToWait > 0 && mySerial.available()==0) {
    delay(100);
    timeToWait = timeToWait - 1;
  }

  if (timeToWait <= 0) {
    char mesg[BUFFER_SIZE];
    memset(mesg, 0x00, BUFFER_SIZE);
    sprintf(mesg, "%s -- Command timed out", cmd);
    Serial.println(mesg);
    errCode = RESP_TIMEOUT;
    return false;
  }
  data = mySerial.readString();
  
  if(!strcmp(cmd, CMD_GET_ID)) {
    if (GetId(data, errCode)) {
      data.toCharArray(idCharArray, 24);
      return true;
    }
    
    strcpy(idCharArray, "Unknown");
    return false;
  }
  
  Serial.println(data);
  delay(100);

  data.toCharArray(dataChar, BUFFER_SIZE);
  
  if (isHttpAction) {
    return isHttpActionDone(dataChar);
  }
  return isResponseDone(dataChar);
}

/***************************************************
 *  Name:        sendCommand
 *
 *  Returns:     True if SIM900 command has been sent
 *
 *  Parameters:  None
 *
 *  Description: Sends a AT+ command to the SIM900
 *
 ***************************************************/

boolean sendCommand(const char *cmd, boolean isHttpAction)
{
  boolean bResponseDone = false;
  
  mySerial.println(cmd);
  
  do {
    RESP_CODE errCode = RESP_ERROR;
    bResponseDone = waitForOk(cmd, isHttpAction, errCode);
    if (errCode != RESP_OK) {
      return false;
    }
  } while(!bResponseDone);
  
  return true;
}

/***************************************************
 *  Name:        sendSMS
 *
 *  Returns:     Nothing
 *
 *  Parameters:  None
 *
 *  Description: Sends an SMS
 *
 ***************************************************/
/*
void sendSMS() {

  mySerial.print("AT+CMGF=1\r");    //Because we want to send the SMS in text mode
  delay(500);
  mySerial.println("AT + CMGS = \"+12039808543\"");
  delay(500);
  mySerial.println("Hello from Kaushik's SIM900!");//the content of the message
  delay(500);

  mySerial.println((char)26);//the ASCII code of the ctrl+z is 26
  delay(100);
  mySerial.println();  
}
*/

/***************************************************
 *  Name:        SetupHttp
 *
 *  Returns:     True if SIM900 HTTP module is set up
 *
 *  Parameters:  None
 *
 *  Description: Intializes and sets up the HTTP module
 *
 ***************************************************/

boolean SetupHttp()
{  
  if(!sendCommand("AT+HTTPTERM", false)) {
    return false;
  }
  if(!sendCommand("AT+CSQ", false)) {
    return false;
  }
  if(!sendCommand("AT+CGATT?", false)) {
    return false;
  }
  if(!sendCommand("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"", false)) {
    return false;
  }
  if(!sendCommand("AT+SAPBR=3,1,\"APN\",\"CMNET\"", false)) {
    return false;
  }
  if(!sendCommand("AT+SAPBR=1,1", false)) {
    return false;
  }
  if(!sendCommand("AT+SAPBR=2,1", false)) {
    return false;
  }
  return true;
}

/***************************************************
 *  Name:        CloseHttp
 *
 *  Returns:     True if SIM900 HTTP module is closed cleanly
 *
 *  Parameters:  None
 *
 *  Description: Closes the HTTP module and releases the bearer
 *
 ***************************************************/
boolean CloseHttp()
{
  if(!sendCommand("AT+HTTPTERM", false)) {
    return false;
  }
  if(!sendCommand("AT+SAPBR=0,1", false)) {
    return false;
  }
  return true;
}

/***************************************************
 *  Name:        GetId
 *
 *  Returns:     True if SIM900 ID has been obtained
 *
 *  Parameters:  None
 *
 *  Description: Gets the ID of the SIM card
 *
 ***************************************************/
boolean GetId(String &idStr, RESP_CODE &errCode)
{    
  if (false == idStr.startsWith(CMD_GET_ID)) {
    errCode = RESP_ERROR;
    return false;
  }
  int pos = idStr.indexOf("\r\n");
  if (-1 == pos) {
    errCode = RESP_ERROR;
    return false;
  }
  
  idStr.remove(0, pos);
  idStr.trim();
  
  pos = idStr.indexOf("OK");
  if (-1 == pos) {
    errCode = RESP_ERROR;
    return false;
  }  
  idStr.setCharAt(pos-1, '\0');
  idStr.trim();

  pos = idStr.indexOf("\r\n");
  if (-1 == pos) {
    errCode = RESP_ERROR;
    return false;
  }  
  idStr.setCharAt(pos-1, '\0');
  idStr.trim();
  
  return true;
}

/***************************************************
 *  Name:        SubmitPostRequest
 *
 *  Returns:     True if SIM900 POST request has been sent successfully
 *
 *  Parameters:  None
 *
 *  Description: Reads the sensor value and sends it to the Server
 *
 ***************************************************/

boolean SubmitPostRequest()
{
  char bufferToSend[BUFFER_SIZE];
  char lengthData[JSON_LENGTH];

  memset(bufferToSend, 0x00, BUFFER_SIZE);
  memset(lengthData, 0x00, JSON_LENGTH);
  
  sprintf(bufferToSend, "{\"device_id\":\"%s\", \"value\":\"%d\"}", idCharArray, readSensorValue());

  if(!sendCommand("AT+CSQ", false)) {
    return false;
  }
  if(!sendCommand("AT+HTTPINIT", false)) {
    return false;
  }
  if(!sendCommand("AT+HTTPPARA=\"CID\",1", false)) {
    return false;
  }
  if(!sendCommand("AT+HTTPPARA=\"URL\",\"http://posttestserver.com/post.php?dir=myano\"", false)) {
    return false;
  }
  if(!sendCommand("AT+HTTPPARA=\"CONTENT\",\"application/json\" ", false)) {
    return false;
  }
  
  strcpy(lengthData, "");
  sprintf(lengthData, "AT+HTTPDATA=%d,10000", strlen(bufferToSend));
  if(!sendCommand(lengthData, false)) {
    return false;
  }
  
  if(!sendCommand(bufferToSend, false)) {
    return false;
  }
  if(!sendCommand("AT+HTTPACTION=1", true)) {
    return false;
  } 
  if(!sendCommand("AT+HTTPREAD", false)) {
    return false;
  }
  return true;
}

