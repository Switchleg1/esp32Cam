#include <arduino.h>
#include "camera.h"
#include "fat32.h"
#include "avi.h"
#include "globals.h"
#include "bluetooth.h"

uint8_t fileNumber;

void dataReceived(uint8_t* src, uint16_t size)
{
  
}

void openNextAvi()
{
  char fileName[256];
  sprintf(fileName, "/myavi%d.avi", fileNumber++);
  Camera.startFile(fileName, 50);
}

void setup()
{
  Serial.begin(500000);
  Serial.setDebugOutput(true);
  Serial.println();

  while(!Fat32.begin());
  Fat32.listDir("/", 0);

  CBluetoothCB callbacks = {
    .dataReceived = dataReceived,
    .onConnect    = NULL,
    .onDisconnect = NULL
  };

  //Bluetooth.begin(callbacks);
  
  while(Camera.begin() != CAM_RET_OK);
}

void loop()
{
  delay(100);
}
