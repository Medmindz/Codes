#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <time.h>
#include <twilio.hpp>



static const char *account_sid = "<your_twilio_account_sid";
static const char *auth_token = "your_twilio_account_auth_token";
static const char *from_number = "+12676338371";
static const char *to_number = "+94784445246";
static const char *message = "Sent from my ESP32";



Twilio *twilio;
const int mpuAddress = 0x68;                       // I2C address of the MPU-6050

float xByGyro, yByGyro, zByGyro, xA, yA, zA;       // Global variables for the rotation by gyro
int fallcheck = 0;
int sleepcheck = 0;                                // 0 for awake, 1 for asleep Assume device is placed when person is lying down
int walking = 0;                                   // 0 for not walking, 1 for walking
int gotup = 0;                                     // 0 for not got up, 1 for got up
float resAcc, resAccXZ;


const char* ssid = "Wokwi-GUEST";
const char* password = "";

const char* mqtt_server = "mqtt-dashboard.com";
const int mqtt_port = 1883;
const char* clientId = "clientId-j0x9ZjJyiB";
const char* mqtt_username = "";
const char* mqtt_password = "";

//Switch on the clients
WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_MPU6050 mpu;

//messagebuffer
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (50)
char msg [MSG_BUFFER_SIZE];

//wificonnection setup
void setup_wifi() {
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println("\"" + String(ssid) + "\"");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

//MQTT Server Connection
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect(clientId)) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("check", "Start sending");
      // ... and resubscribe
      client.subscribe("test data");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(1000);
    }
  }
}

//Message Sending
void publishMessage(const char* topic, String payload, boolean retained) {
  if (client.publish (topic, payload.c_str(), true))
    Serial.println("Message Published [" + String(topic) + "]: " + payload);
}



void setup() {
  Serial.begin(9600);
  Wire.begin();


  while (!Serial) delay(1);
  setup_wifi();
  client.setServer (mqtt_server, mqtt_port);

  twilio = new Twilio(account_sid, auth_token);

  delay(1000);

  String response;
  bool success = twilio->send_message(to_number, from_number, message, response);
  if (success) {
    Serial.println("Sent message successfully!");
  } else {
    Serial.println(response);
  }

  Wire.beginTransmission( mpuAddress);
  Wire.write( 0x6B);                           // PWR_MGMT_1 register
  Wire.write( 0);                              // set to zero (wakes up the MPU-6050)
  auto error = Wire.endTransmission();

  if ( error != 0)
  {
    Serial.println(F( "Check connection with MPU 6050"));
    for (;;);                                  // halt the sketch if error encountered
  }

  // Initialize the time
  configTime(19800, 0, "pool.ntp.org");
  while (time(nullptr) < 1000) {
    delay(1000);
    Serial.println("Waiting for NTP time...");
  }

}


void loop() {
  String response;

  if (!client.connected()) reconnect();
  client.loop();

  Wire.beginTransmission( mpuAddress);
  Wire.write( 0x3B);                   // Starting with register 0x3B (ACCEL_XOUT_H)
  Wire.endTransmission( false);        // No stop condition for a repeated start


  time_t now = time(nullptr);
  char time_string[20];
  strftime(time_string, sizeof(time_string), "%Y-%m-%d %H:%M:%S", localtime(&now));
  Serial.print("Current time and date: ");
  Serial.println(time_string);



  // The MPU-6050 has the values as signed 16-bit integers.
  // There are 7 values in 14 registers.
  int16_t AcX, AcY, AcZ, Tmp, GyX, GyY, GyZ;

  Wire.requestFrom( mpuAddress, 14);   // request a total of 14 bytes
  AcX = Wire.read()<<8 | Wire.read();  // 0x3B (ACCEL_XOUT_H) & 0x3C (ACCEL_XOUT_L)    
  AcY = Wire.read()<<8 | Wire.read();  // 0x3D (ACCEL_YOUT_H) & 0x3E (ACCEL_YOUT_L)
  AcZ = Wire.read()<<8 | Wire.read();  // 0x3F (ACCEL_ZOUT_H) & 0x40 (ACCEL_ZOUT_L)
  Tmp = Wire.read()<<8 | Wire.read();  // 0x41 (TEMP_OUT_H)   & 0x42 (TEMP_OUT_L)
  GyX = Wire.read()<<8 | Wire.read();  // 0x43 (GYRO_XOUT_H)  & 0x44 (GYRO_XOUT_L)
  GyY = Wire.read()<<8 | Wire.read();  // 0x45 (GYRO_YOUT_H)  & 0x46 (GYRO_YOUT_L)
  GyZ = Wire.read()<<8 | Wire.read();  // 0x47 (GYRO_ZOUT_H)  & 0x48 (GYRO_ZOUT_L)

  // The acceleration is directly mapped into the angles.


  float xByAccel = (float) AcX * 0.0001;      // static angle by accelerometer
  float yByAccel = (float) AcY * 0.0001;
  float zByAccel = (float) AcZ * 0.0001;

  xByGyro += (float) GyX * 0.00001;           // moving angle by gyro
  yByGyro += (float) GyY * 0.00001;
  zByGyro += (float) GyZ * 0.00001;

  float x = xByAccel + xByGyro;               // combine both angles
  float y = yByAccel + yByGyro;
  float z = zByAccel + zByGyro;

  // converting acceleration values in to ms-2
  xA = AcX/16384.0;
  yA = AcY/16384.0;
  zA = AcZ/16384.0;
  
// detecting whether person wakes up from bed
//Assume person is wearing it in the orientation
//Y axis is the longitunal axis of the body
//X axis is the frontal axis of the body
//Z axis is the sagital axis of the body

// approximate values
// When the person is standing up the acceleration in the Y axis is 1g
// When the person is lying down the acceleration in the Y axis is 0g
// When the person is lying down the resultant acceleration in the X, Z axis is 1g(aY = 0)

resAcc = sqrt(xA*xA + yA*yA + zA*zA);
resAccXZ = sqrt(xA*xA + zA*zA);

//detect whether person fallen when walking
if (resAcc < 0.3){
  fallcheck = 1;
}
else if(fallcheck == 1){
  if(resAcc>1.5) {
    fallcheck = 0;
    //send message to the server
    publishMessage("Notifications", "[" + String(time_string) + "]: Abnormal Movement Detected - Fallen to ground", false);
    bool success =twilio->send_message(to_number, from_number, ("Notifications", "[" + String(time_string) + "]: Abnormal Movement Detected - Fallen to ground"), response);

  }
}


if(abs(yA)<1.15 && abs(yA)>0.85){
  sleepcheck = 0;
  if(resAcc > 1.1 && walking == 0){
    walking = 1;
    // send message to server he is walking
    publishMessage("Notifications", "[" + String(time_string) + "]: Abnormal Movement Detected - Walking in the house", false);
    bool success =twilio->send_message(to_number, from_number, ("Notifications", "[" + String(time_string) + "]: Abnormal Movement Detected - Walking in the house"), response);

  }
  if(resAccXZ < 0.1 && abs(xByGyro) + abs(zByGyro) >2 && gotup==0){
    gotup = 1;
    // send message to server he gotup from the bed
    publishMessage("Notifications", "[" + String(time_string) + "]: Abnormal Movement Detected - Getup from the bed", false);
    bool success =twilio->send_message(to_number, from_number, ("Notifications", "[" + String(time_string) + "]: Abnormal Movement Detected - Get up from bed"), response);

    xByGyro = 0;
    zByGyro = 0;
    delay(10000);
  }
  }
if(resAccXZ > 0.8 && resAcc < 1.2){
    sleepcheck = 1;
    gotup = 0;
    walking = 0;
  }
}


