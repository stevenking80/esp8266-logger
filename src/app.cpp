#include <ESP8266WiFi.h>
#include <OneWire.h>
#include <AmazonDynamoDBClient.h>
#include <ESP8266AWSImplementations.h>

// global variables 
#define TEMP_SENSOR_PIN D2
#define WIFI_SSID "ssid"
#define WIFI_PASSWD "passwd"
#define AWS_REGION "us-east-2"
#define AWS_ACCESS_KEY "access"
#define AWS_SECRET_KEY "secret"
#define AWS_DDB_TABLE_NAME "test"

// Temperature sensor on D2
static int oneWirePin = TEMP_SENSOR_PIN;
OneWire ds(oneWirePin);

// AWS DynamoDB
Esp8266HttpClient httpClient;
AmazonDynamoDBClient ddbClient;
Esp8266DateTimeProvider dateTimeProvider;
static const char* ddbTableName = AWS_DDB_TABLE_NAME;

// TODO: A web server to show the current temperature

/**
 * Prints / formats the temperature degrees celsius
 * 
 * @param data
 */
String tempToString(byte* data) { 
  int HighByte, LowByte, TReading, SignBit, Tc_100, Whole, Fract;

  LowByte = data[0];
  HighByte = data[1];
  TReading = (HighByte << 8) + LowByte;
  SignBit = TReading & 0x8000;  // test most sig bit
  if (SignBit) // negative
  {
    TReading = (TReading ^ 0xffff) + 1; // 2's comp
  }
  Tc_100 = (6 * TReading) + TReading / 4;    // multiply by (100 * 0.0625) or 6.25

  Whole = Tc_100 / 100;  // separate off the whole and fractional portions
  Fract = Tc_100 % 100;

  String formatted = "";
  if (SignBit) // If its negative
  {
     formatted += "-";
  }
  formatted += String(Whole);
  formatted += ".";
  if (Fract < 10)
  {
     formatted += "0";
  }
  formatted += String(Fract);

  return formatted;
}

String addrToString(byte* addr) {
  String formatted = "";
  for(int i = 0; i < 8; i++) {
    formatted += String(addr[i], HEX);
  }

  return formatted;
}

String toString(MinimalString s) { 
  if(s.length() > 0) {
    return String(s.getCStr());
  }
  return String("");
}

void reportTemp(String id, String temperature) { 

  // Device (identifier for the temperature probe)
  AttributeValue device;
  device.setS(id.c_str());

  // Timestamp
  AttributeValue attrTimestamp;
  attrTimestamp.setS(MinimalString(dateTimeProvider.getDateTime()));

  // Temperature
  AttributeValue attrTemp;
  attrTemp.setS(MinimalString(temperature.c_str()));

  MinimalKeyValuePair<MinimalString, AttributeValue> attributes[3] = { 
    MinimalKeyValuePair<MinimalString, AttributeValue>(MinimalString("device"), device),
    MinimalKeyValuePair<MinimalString, AttributeValue>(MinimalString("timestamp"), attrTimestamp),
    MinimalKeyValuePair<MinimalString, AttributeValue>(MinimalString("temperature"), attrTemp) 
  };
  
  PutItemInput input;
  input.setTableName(MinimalString(ddbTableName));
  input.setItem(MinimalMap<AttributeValue>(attributes, 3));

  ActionError actionError;
  int httpStatusCode = 0;
  PutItemOutput output = ddbClient.putItem(input, actionError);

  switch (actionError) {
    case NONE_ACTIONERROR:
        Serial.println("PutItem succeeded!");
        break;
    case INVALID_REQUEST_ACTIONERROR:
        Serial.print("ERROR: ");
        Serial.println(output.getErrorMessage().getCStr());
        break;
    case MISSING_REQUIRED_ARGS_ACTIONERROR:
        Serial.println(
                "ERROR: Required arguments were not set for PutItemInput");
        break;
    case RESPONSE_PARSING_ACTIONERROR:
        Serial.println("ERROR: Problem parsing http response of PutItem");
        break;
    case CONNECTION_ACTIONERROR:
        Serial.println("ERROR: Connection problem");
        break;
    }
}

/**
 * Arduino: setup the board
 */
void setup()
{
  Serial.begin(115200);
  Serial.println();
  Serial.print("Starting ...");
  Serial.println();

  WiFi.begin(WIFI_SSID, WIFI_PASSWD);

  Serial.print("Connecting to " WIFI_SSID " ...");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  Serial.print("Connected, IP address: ");
  Serial.println(WiFi.localIP());

  // TODO: Set an LED

  // TODO: Set up the single wire interface 

  // Configure the DDB client
  ddbClient.setAWSRegion(AWS_REGION);
  ddbClient.setAWSEndpoint("amazonaws.com");
  ddbClient.setAWSKeyID(AWS_ACCESS_KEY);
  ddbClient.setAWSSecretKey(AWS_SECRET_KEY);
  ddbClient.setHttpClient(&httpClient);
  ddbClient.setDateTimeProvider(&dateTimeProvider);
  ddbClient.setHTTPS(true);
}

void loop() {
  
  byte i;
  byte present = 0;
  byte data[12];
  byte addr[8];

  ds.reset_search();
  if ( !ds.search(addr)) {
      Serial.print("No more addresses.\n");
      ds.reset_search();
      delay(5000);
      return;
  }

  String id = addrToString(addr);
  Serial.println("R=" + id);

  if ( OneWire::crc8( addr, 7) != addr[7]) {
      Serial.print("CRC is not valid!\n");
      return;
  }

  if ( addr[0] == 0x10) {
      Serial.print("Device is a DS18S20 family device.\n");
  }
  else if ( addr[0] == 0x28) {
      Serial.print("Device is a DS18B20 family device.\n");
  }
  else {
      Serial.print("Device family is not recognized: 0x");
      Serial.println(addr[0],HEX);
      delay(5000);
      return;
  }

  ds.reset();
  ds.select(addr);
  ds.write(0x44,1);         // start conversion, with parasite power on at the end

  delay(1000);     // maybe 750ms is enough, maybe not
  // we might do a ds.depower() here, but the reset will take care of it.

  present = ds.reset();
  ds.select(addr);    
  ds.write(0xBE);         // Read Scratchpad

  Serial.print("P=");
  Serial.print(present,HEX);
  Serial.print(" ");
  for ( i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = ds.read();
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.print(" CRC=");
  Serial.print( OneWire::crc8( data, 8), HEX);
  Serial.println();

  // Get the temperature as a string
  String temp = tempToString(data);
  Serial.print("Recorded temperature: ");
  Serial.print(temp);
  Serial.println();
  Serial.flush();

  // Report the temperature
  reportTemp(id, temp);

  Serial.println("Sleeping until next read");
  delay(5 * 60 * 1000); // 5 minutes
}
