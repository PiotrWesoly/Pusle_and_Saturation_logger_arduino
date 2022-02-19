/******************************************************************************************************
 *                                        Includes                                                    *
 *****************************************************************************************************/
#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"

#include "BluetoothSerial.h"
#include <string>

using namespace std;
/******************************************************************************************************
 *                                       Defines                                                      *
 *****************************************************************************************************/
#define BUFFER_LENGTH       11100
#define MAX_BRIGHTNESS      255

/******************************************************************************************************
 *                                      Typedefs                                                      *
 *****************************************************************************************************/
typedef struct Readings
{
  uint8_t heartRate;
  uint8_t spO2;
  uint32_t sampleTime;
} Readings_struct;

/******************************************************************************************************
 *                                      Global variables                                              *
 *****************************************************************************************************/
/********** Bluetooth object ************/
BluetoothSerial SerialBT;

/********** Sensor variables ************/
uint16_t bufferIndex=0;
bool firstConnection=true;

/* Buffer allocation */
Readings_struct readings[BUFFER_LENGTH];
//Readings_struct* readings = (Readings_struct*) malloc(150000 * sizeof(Readings_struct));

MAX30105 particleSensor;

uint32_t irBuffer[100]; //infrared LED sensor data
uint32_t redBuffer[100];  //red LED sensor data

int32_t bufferLength; //data length
int32_t spo2; //SPO2 value
int8_t validSPO2; //indicator to show if the SPO2 calculation is valid
int32_t heartRate; //heart rate value
int8_t validHeartRate; //indicator to show if the heart rate calculation is valid


/******************************************************************************************************
 *                                      Setup                                                          *
 *****************************************************************************************************/
void setup() {
  /************************* Initialize bluetooth ***********************/
  SerialBT.begin("Pulse and saturation logger");
  Serial.begin(115200);
  
  /************************* Initialize sensor ***********************/
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) //Use default I2C port, 400kHz speed
  {
    Serial.println(F("MAX30105 was not found. Please check wiring/power."));
    while (1);
  }

  Serial.println(F("Attach sensor to finger with rubber band. Press any key to start conversion"));
  while (Serial.available() == 0) ; /* wait until user presses a key */
  Serial.read();

  /********************************   Setup configurations **********************************/
  byte ledBrightness = 60; //Options: 0=Off to 255=50mA
  byte sampleAverage = 4; //Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 2; //Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
  byte sampleRate = 100; //Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411; //Options: 69, 118, 215, 411
  int adcRange = 4096; //Options: 2048, 4096, 8192, 16384
  
  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); //Configure sensor with these settings
}

/******************************************************************************************************
 *                                      Loop                                                          *
 *****************************************************************************************************/
void loop()
{
  bufferLength = 100; /* buffer length of 100 stores 4 seconds of samples running at 25sps */

  /***************** Read frist 100 samples for range estimation *********************/
  /* read the first 100 samples, and determine the signal range */
  for (byte i = 0 ; i < bufferLength ; i++)
  {
    while (particleSensor.available() == false) /* do we have new data? */
      particleSensor.check(); /* Check the sensor for new data */

    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample(); //We're finished with this sample so move to next sample

    Serial.print(F("red="));
    Serial.print(redBuffer[i], DEC);
    Serial.print(F(", ir="));
    Serial.println(irBuffer[i], DEC);
  }

  /* Calculate heart rate and SpO2 after first 100 samples (first 4 seconds of samples) */
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

  /***************** Continously read new samples (25/reading) *********************/
  /* Continuously taking samples from MAX30102.  Heart rate and SpO2 are calculated every 1 second */
  while (1)
  {
    /* dumping the first 25 sets of samples in the memory and shift the last 75 sets of samples to the top */
    for (byte i = 25; i < 100; i++)
    {
      redBuffer[i - 25] = redBuffer[i];
      irBuffer[i - 25] = irBuffer[i];
    }

    /* take 25 sets of samples before calculating the heart rate. */
    for (byte i = 75; i < 100; i++)
    {
      while (particleSensor.available() == false) //do we have new data?
        particleSensor.check(); //Check the sensor for new data

      redBuffer[i] = particleSensor.getRed();
      irBuffer[i] = particleSensor.getIR();
      particleSensor.nextSample(); //We're finished with this sample so move to next sample
    }

    /* After gathering 25 new samples recalculate HR and SP02 */
     maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

      /****************************  Save readings to the buffer ***************************/
     if(heartRate != -999)
     {
     Serial.print(F(", HR="));
     Serial.print(heartRate, DEC);
     Serial.print(F(", spo2="));
     Serial.println(spo2, DEC);
     readings[bufferIndex].heartRate = (uint8_t) heartRate;
     readings[bufferIndex].spO2 = (uint8_t) spo2;
     readings[bufferIndex].sampleTime = millis();
     }
     else
     {
      Serial.println(F("wrong reading"));
     }

     /**************************** When connected to phone ********************************/
     Serial.println(SerialBT.available());
     Serial.println(bufferIndex);
      if(SerialBT.available())
      {
        if(firstConnection == true)   /* On first connection send the history of readings */
        {
          for(uint32_t i =0; i<=bufferIndex; i++)
          {       
            /* Print buffer to serial monitor */
            Serial.print("Buffer: ");
            Serial.println(readings[i].heartRate, DEC);

            /* Send previous readings */
            SerialBT.write(readings[i].heartRate);
            SerialBT.write(readings[i].spO2);
            SerialBT.write(readings[i].sampleTime);
            
          }
          firstConnection = false;
        }
        else                          /* After receiving all history keep sending current reading */
        {
          SerialBT.write(readings[bufferIndex].heartRate);
          SerialBT.write(readings[bufferIndex].spO2);
          SerialBT.write(readings[bufferIndex].sampleTime);
        }
      }
      bufferIndex++;
      if(bufferIndex >= BUFFER_LENGTH) bufferIndex = 0;
  }
}