/*
Print out the date/time along with the temperature and humidity at regular intervals.
 */
#include <stdio.h>
#include <string.h>
#include <DS1302RTC_c.h>
#include <DHT.h>
#include <SdFat.h>

/* digital I/O pin for DHT22 */
#define DHT_PIN 2
#define DHT_TYPE DHT22
/* digital I/O pins for light sensor */
#define LDR_PIN 3
/* digital I/O pins for SD reader (Chip Select PIN) */
#define CS_PIN 4
/* digital I/O pins for RTC */
#define CE_PIN 5
#define IO_PIN 6
#define SCLK_PIN 7
/* RELAY PINS */
#define LIGHT_RELAY_PIN 8
#define HUMIDIFIER_RELAY_PIN 9
#define FAN_RELAY_PIN 10

#define ARRAYSIZE(x) (sizeof(x)/sizeof(*(x)))
#define HUMIDITY_OVERSHOOT 5

DS1302RTC rtc(CE_PIN, IO_PIN, SCLK_PIN);
DHT dht(DHT_PIN, DHT_TYPE);

/* buffers for RTC */
char buf[50];
char day[10];

int lights_settings[12][4];
int temperature_settings[12];
int humidity_settings[12];

char lastEntryDatetime[17];

//store settings from DHT22
float humidity_sensor;
float temperature_sensor;
boolean humidifier_status = false;
boolean fan_status = false;
uint8_t light_sensor;
ds1302_struct rtc_sensor;
SdFat sd;
SdFile myFile;

int logged_hour = 0;

void load_settings()
{
  Serial.print("Initializing SD card...");
  // Initialize SdFat or print a detailed error message and halt
  // Use half speed like the native library.
  // change to SPI_FULL_SPEED for more performance.
  if (!sd.begin(CS_PIN, SPI_FULL_SPEED)) sd.initErrorHalt();
  Serial.println("Card initialized.");
  if (!myFile.open("/settings/settings.txt", O_READ)) {
    sd.errorHalt("opening /settings/settings.txt for read failed");
  }

  char buffer[5], oneByte;
  memset(&buffer[0], 0, sizeof(buffer));
  size_t i=0, j=0, record_cnt=0, month=1;
  while ((oneByte = myFile.read()) >= 0) 
  {
    if (oneByte != ',' && oneByte != ':' && oneByte != '\n')
    {
      buffer[i++] = oneByte;
    }
    else
    {
      switch (record_cnt) 
      {
      case 0:
        /*
            Serial.print("Month:");
         Serial.println(buffer);
         */
        month = atoi(buffer);
        break;
      case 1:
      case 2:
      case 3:
      case 4:
        /*
            Serial.print(month-1);
         Serial.print(":");
         Serial.print(record_cnt-1);
         Serial.print(":");
         Serial.print(buffer);
         Serial.println(":");
         */
        lights_settings[month-1][record_cnt-1] = atoi(buffer);
        break;
      case 5:
        temperature_settings[month-1] = atoi(buffer);
        break;
      case 6:
        humidity_settings[month-1] = atoi(buffer);
        break;
      default:
        break;
      }
      if (oneByte == '\n') 
      {
        record_cnt=0;
        memset(&buffer[0], 0, sizeof(buffer));
      } 
      else {
        record_cnt++;
      }
      i = 0;
    }
  }
  myFile.close();

  //print the array
  for (i=0; i<12; i++)
  {
    for (j=0; j<4; j++)
    {
      Serial.print(lights_settings[i][j]);
      Serial.print(":");
    }
    Serial.print(";");
    Serial.print(temperature_settings[i]);
    Serial.print(";");
    Serial.print(humidity_settings[i]);
    Serial.println();
  }

  //grab last entry from file.
  char filename[14];
  char entry[31];
  char currentChar;
  memset(&filename[0], 0, sizeof(filename));
  /* Get the current time and date from the chip */
  rtc.clock_burst_read( (uint8_t *) &rtc_sensor);
  snprintf(filename, sizeof(filename), "logs/%04d.txt", 2000+bcd2bin(rtc_sensor.Year10, rtc_sensor.Year));

  //Open the file if it exists, and read the last entry. If not, just ignore.
  if (myFile.open(filename, O_READ)) {
    Serial.println("Grabbing last entry from logging file...");
    if(myFile.available() && myFile.fileSize() >= sizeof(entry))
    {
      myFile.seekSet(myFile.fileSize() - sizeof(entry) - 1);
      memset(&lastEntryDatetime[0], 0, sizeof(lastEntryDatetime));
      int cnt = 0, commaCnt = 0;
      while (commaCnt < 2)
      {
        currentChar = myFile.read();
        if (currentChar == ',') commaCnt++;
        if (commaCnt < 2)
          lastEntryDatetime[cnt++] = currentChar;
      }
      Serial.println(lastEntryDatetime);
    }
    myFile.close();
  }

}

void reset_clk()
{
  if (bcd2bin( rtc_sensor.Seconds10, rtc_sensor.Seconds) > 59) {
    Serial.println("Errorous time, resetting seconds.");
    int seconds = 0;
    rtc_sensor.Seconds = bin2bcd_l(seconds);
    rtc_sensor.Seconds10 = bin2bcd_h(seconds);
    rtc_sensor.CH = 0;
    rtc_sensor.WP = 0;
    rtc.clock_burst_write( (uint8_t *) &rtc_sensor);
  }
}

void print_time()
{
  /* Format date and insert into the temporary buffer */
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d,%02d:%02d:%02d", 
  2000+bcd2bin(rtc_sensor.Year10, rtc_sensor.Year), 
  bcd2bin(rtc_sensor.Month10, rtc_sensor.Month),
  bcd2bin(rtc_sensor.Date10, rtc_sensor.Date),
  bcd2bin(rtc_sensor.h24.Hour10, rtc_sensor.h24.Hour),
  bcd2bin(rtc_sensor.Minutes10, rtc_sensor.Minutes),
  bcd2bin(rtc_sensor.Seconds10, rtc_sensor.Seconds));

  /* Print the formatted string to serial so we can see the time */
  Serial.println(buf);
}

void read_sensors()
{
  temperature_sensor = dht.readTemperature();
  humidity_sensor = dht.readHumidity();
  light_sensor = digitalRead(LDR_PIN);
  /* Get the current time and date from the chip */
  rtc.clock_burst_read( (uint8_t *) &rtc_sensor);

  Serial.print("Temperature: ");
  Serial.println(temperature_sensor);
  Serial.print("Humidity: ");
  Serial.println(humidity_sensor);
  Serial.print("Ambient: ");
  if (light_sensor == HIGH)
    Serial.println("DARK  ");
  else
    Serial.println("BRIGHT");
}

void control_environment()
{
  Serial.print("Lights: ");
  /*  
   Serial.println(lights_settings[rtc_sensor.mon][0] * 60 + lights_settings[rtc_sensor.mon][1]);
   Serial.println(lights_settings[rtc_sensor.mon][2] * 60 + lights_settings[rtc_sensor.mon][3]);
   Serial.println(rtc_sensor.hr * 60 + rtc_sensor.min);
   */

  //if the time is within this month's schedule, and the it's not too bright already.
  if (bcd2bin(rtc_sensor.h24.Hour10, rtc_sensor.h24.Hour) * 60 + bcd2bin(rtc_sensor.Minutes10, rtc_sensor.Minutes) >= lights_settings[bcd2bin(rtc_sensor.Month10, rtc_sensor.Month)-1][0] * 60 + lights_settings[bcd2bin(rtc_sensor.Month10, rtc_sensor.Month)-1][1]
    && bcd2bin(rtc_sensor.h24.Hour10, rtc_sensor.h24.Hour) * 60 + bcd2bin(rtc_sensor.Minutes10, rtc_sensor.Minutes) <= lights_settings[bcd2bin(rtc_sensor.Month10, rtc_sensor.Month)-1][2] * 60 + lights_settings[bcd2bin(rtc_sensor.Month10, rtc_sensor.Month)-1][3]
    && light_sensor == HIGH)
  {
    digitalWrite(LIGHT_RELAY_PIN, HIGH);
    Serial.println("ON");
  } 
  else {
    digitalWrite(LIGHT_RELAY_PIN, LOW);
    Serial.println("OFF");
  }

  //controlling humidifier/fan logic.
  if (!isnan(humidity_sensor)) {
    //Serial.print(humidity_settings[bcd2bin(rtc_sensor.Month10, rtc_sensor.Month)-1]);
    if (humidity_sensor < humidity_settings[bcd2bin(rtc_sensor.Month10, rtc_sensor.Month)-1]) {
      //turn on humidifier if humidity falls below humidity target.
      humidifier_status = true;
      fan_status = true;
    } 
    else {
      if (humidity_sensor > humidity_settings[bcd2bin(rtc_sensor.Month10, rtc_sensor.Month)-1] + HUMIDITY_OVERSHOOT) {
        if (humidifier_status) {
          //if humidifier is on, then we only stop it after humidity overshoots target by HUMIDITY_OVERSHOOT.
          humidifier_status = false;
          fan_status = false;
        } 
        else {
          //if humidifier is off and humidity is above humidity target + HUMIDITY_OVERSHOOT, then turn on fan to lower humidity.
          //fan_status = true;
        }
      } 
      else {
        if (!humidifier_status) {
          //if humidifier is not on, and the humidity is below humidity target + HUMIDITY_OVERSHOOT, then we can turn off the fan.
          //fan_status = false;
        }
      }
    }
  } 
  else {
    //if no reading for humidity, then turn off humidifier.
    humidifier_status = false;
    fan_status = false;
  }

  if (!isnan(temperature_sensor)) {
    if (temperature_sensor > temperature_settings[bcd2bin(rtc_sensor.Month10, rtc_sensor.Month)-1]) {
      //if it's too hot, turn on the fan to cool down.
      fan_status = true;
    } else {
      if (!humidifier_status && (temperature_sensor < temperature_settings[bcd2bin(rtc_sensor.Month10, rtc_sensor.Month)-1]))
      //if the humidifier is not on, and if not too hot then turn off of the fan.
      fan_status = false;
    }
  }

  //actually change the PIN values to control humidifier and fan.
  Serial.print("Humidifier: ");
  if (humidifier_status) {
    digitalWrite(HUMIDIFIER_RELAY_PIN, HIGH);
    Serial.println("ON");
  } 
  else {
    digitalWrite(HUMIDIFIER_RELAY_PIN, LOW);
    Serial.println("OFF");
  }
  Serial.print("Fan: ");
  if (fan_status) {
    digitalWrite(FAN_RELAY_PIN, HIGH);
    Serial.println("ON");
  } 
  else {
    digitalWrite(FAN_RELAY_PIN, LOW);
    Serial.println("OFF");
  }
}

void log_temp_and_humidity()
{
  // check if returns are valid, if they are NaN (not a number) then something went wrong!
  if (logged_hour != bcd2bin(rtc_sensor.h24.Hour10, rtc_sensor.h24.Hour)) 
  {
    char filename[14];
    memset(&filename[0], 0, sizeof(filename));
    snprintf(filename, sizeof(filename), "logs/%04d.txt", 2000+bcd2bin(rtc_sensor.Year10, rtc_sensor.Year));

    //read the file to see if the last entry is already written for this hour.
    char datetime[17];
    char buffer[6];
    char entry[31];
    char currentChar;

    snprintf(datetime, sizeof(datetime), "%04d-%02d-%02d,%02d:00", 
    2000+bcd2bin(rtc_sensor.Year10, rtc_sensor.Year), 
    bcd2bin(rtc_sensor.Month10, rtc_sensor.Month),
    bcd2bin(rtc_sensor.Date10, rtc_sensor.Date),
    bcd2bin(rtc_sensor.h24.Hour10, rtc_sensor.h24.Hour));
    Serial.println(datetime);

    Serial.print("Logging file: ");
    Serial.println(filename);

    if (strcmp(lastEntryDatetime, datetime) != 0) 
    {
      if (!(isnan(temperature_sensor) || isnan(humidity_sensor)))
      {
        Serial.print("Writing to file: ");
        Serial.println("Logging temperature and humidity: ");
        Serial.println(filename);
          memset(&entry[0], 0, sizeof(entry));
          strcat(entry,datetime);
          dtostrf(temperature_sensor,sizeof(buffer),2,buffer);
          strcat(strcat(entry,","),buffer);
          dtostrf(humidity_sensor,sizeof(buffer),2,buffer);
          strcat(strcat(entry,","),buffer);
        if (myFile.open(filename, O_RDWR | O_CREAT | O_AT_END)) {
          myFile.println(entry);
          myFile.close();
          //update the lastEntryDatetime to the current datetime (that was just written)
          strcpy(lastEntryDatetime, datetime);
          Serial.println(entry);
        } 
        else {
          Serial.println("opening logs for write failed -- not writing.");
        }
      } 
      else {
        //        Serial.println("No valid reading, not writing to SD.");
      }
    } 
    else {
      Serial.println("The entry is already written for this datetime, no need to write again.");
    }
    logged_hour = bcd2bin(rtc_sensor.h24.Hour10, rtc_sensor.h24.Hour);
  } 
}

void setup()
{
  Serial.begin(9600);
  dht.begin();
  pinMode(LDR_PIN, INPUT);
  digitalWrite(LIGHT_RELAY_PIN, LOW);
  pinMode(LIGHT_RELAY_PIN, OUTPUT);
  digitalWrite(HUMIDIFIER_RELAY_PIN, LOW);
  pinMode(HUMIDIFIER_RELAY_PIN, OUTPUT);
  digitalWrite(FAN_RELAY_PIN, LOW);
  pinMode(FAN_RELAY_PIN, OUTPUT);
  load_settings();
  delay(5000);
}

void loop()
{
  read_sensors();
  reset_clk();
  print_time();
  log_temp_and_humidity();
  delay(4000);
  control_environment();
  delay(5000);
}




