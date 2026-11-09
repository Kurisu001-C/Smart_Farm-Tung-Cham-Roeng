#include "ModbusSensor.h"

HardwareSerial modbusSerial(1);

static uint16_t calculateCRC(const uint8_t *buffer, uint8_t length) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < length; i++) {
    crc ^= buffer[i];
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x0001) {
        crc = (crc >> 1) ^ 0xA001;
      } else {
        crc = crc >> 1;
      }
    }
  }
  return crc;
}

void Modbus_Init() {
  modbusSerial.begin(4800, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN);
}

bool Modbus_ReadSoilSensor(uint8_t slaveID, float& humidity, float& temperature) {
  // Read 2 registers (0x0000 Humidity, 0x0001 Temperature) for Soil TH model
  uint8_t req[8];
  req[0] = slaveID;
  req[1] = 0x03;
  req[2] = 0x00;
  req[3] = 0x00;
  req[4] = 0x00;
  req[5] = 0x02; // Read 2 registers
  
  uint16_t crc = calculateCRC(req, 6);
  req[6] = crc & 0xFF;
  req[7] = (crc >> 8) & 0xFF;
  
  while (modbusSerial.available()) modbusSerial.read();
  
  modbusSerial.write(req, sizeof(req));
  modbusSerial.flush();
  
  // Expected response: 3 header bytes (Addr, Func, ByteCount=4) + 4 data bytes + 2 CRC bytes = 9 bytes
  // Robust timeout: 200ms allows for sensor ADC conversion (50-90ms) + 4800 baud turnaround (~18.7ms)
  // Prevents intermittent timeouts / offline fluttering on Sensor ID 2
  unsigned long start = millis();
  while (modbusSerial.available() < 9 && (millis() - start < 200)) {
    delay(2);
  }
  
  if (modbusSerial.available() >= 9) {
    uint8_t resp[9];
    modbusSerial.readBytes(resp, 9);
    
    uint16_t calcCRC = calculateCRC(resp, 7);
    uint16_t rxCRC = resp[7] | (resp[8] << 8);
    
    if (calcCRC == rxCRC && resp[0] == slaveID && resp[1] == 0x03 && resp[2] == 0x04) {
      uint16_t rawHum = (resp[3] << 8) | resp[4];
      int16_t rawTemp = (int16_t)((resp[5] << 8) | resp[6]);
      
      float hum = rawHum / 10.0f;
      float temp = rawTemp / 10.0f;
      
      // Range verification
      if (hum >= 0.0f && hum <= 100.0f && temp >= -40.0f && temp <= 80.0f) {
        humidity = hum;
        temperature = temp;
        return true;
      }
    }
  }
  return false;
}
