// 기초설계 2팀 식물IoT Client - written by Chang-in Baek, 2023/12/01

#include "WiFiEsp.h"  // 와이파이 모듈(esp-01) 라이브러리
#include "DHT.h"      // 온습도 센서 라이브러리
#include "SoftwareSerial.h"
#include <VC0706_UART.h>  // Camera
#include <SPI.h>
#include <avr/boot.h>     // Serial number



// -------------------WiFi setups-------------------
// Emulate Serial1 on pins 6/7 if not present
#ifndef HAVE_HWSERIAL1
#endif
SoftwareSerial Serial1(6, 7); // RX, TX 기존 2,3에서 변경함 (카메라랑 겹쳐서)
// char ssid[] = "AndroidHotspot6638";            // network SSID (name)
const char ssid[] = "U+NetA67E";
// char pass[] = "20010894";        // network password
const char pass[] = "174E3E3AM#";
int status = WL_IDLE_STATUS;     // the Wifi radio's status
const char server[] = "43.200.105.74";  
// const char server[] = "192.168.123.103";
WiFiEspClient client; // Initialize the Ethernet client object



// -------------------Camera setups------------------
SoftwareSerial cameraconnection(2,3);   // RX, TX
VC0706 cam = VC0706(&cameraconnection);



// -------------------Sensor setups------------------
#define DHT_PIN 4                // DHT11을 4번핀에 연결
#define DHT_TYPE DHT11           // DHT11로 설정(DHT22등 사용 가능)
DHT dht(DHT_PIN, DHT_TYPE);      // DHT11 객체 생성



// -------------------initialize functions-----------
int getLight();   // (return Cds to 0~100)
float getTemp();  // (return Temp)
float getHumi();  // (return Humidity)
int getWatery();  // (return Soil watery)
bool cameraInit();  // Initialize camera
void snapShot();    // Take pic & send data to server
void measure();     // measure data & send to server
void printWifiStatus(); // Print Wifi Status
String getSerialNum();  // get serial number
// void reset();      // reboot arduino
void(* reset) (void) = 0;



// -------------------Global Variables---------------
int TIMELAPSE_PERIOD = 12;      // 타임랩스 촬영주기 : 디폴트값 12
const unsigned long interval = 3600000;           // 측정 주기 : 1시간
unsigned long l1 = 0;           // 센서값 측정 타이밍
unsigned long l3 = 0;           // 타임랩스 촬영 타이밍



// *************************SETUP***************************
void setup() {
  // 0. Serial for debug
  Serial.begin(115200);

  // 1. Initialize Sensors & Global Variables
  dht.begin();          // dht 시작
  l1 = 0;
  l3 = 0;

  // 2. Connect WiFi
  Serial1.begin(9600);  // initialize serial for ESP module
  WiFi.init(&Serial1);  // initialize ESP module
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    // don't continue
    while (true);
  }
  while ( status != WL_CONNECTED) {
    // Serial.print("connect to WPA SSID: ");
    // Serial.println(ssid);
    // Connect to WPA/WPA2 network
    status = WiFi.begin(ssid, pass);
  }
  // Serial.println("connected");
  // printWifiStatus();
  // Serial.println();

  Serial.println(getSerialNum());

  // 3. Conncet to Server, POST api/join/ & setup TIMELAPSE_PERIOD
  RingBuffer ringbuf(8);
  client.flush();
  client.stop();
  bool suc = false;
  while(suc != true){
    if (client.connect(server, 8080)) {
      ringbuf.init();

      // API JOIN
      client.println(("POST /api/join/ HTTP/1.1"));
      client.println(("Host: "+String(server)+":8080"));
      client.println(("Accept: */*"));

      //String content = "{\"plant_id\": \"testSerial1\"}";
      String content = "{\"plant_id\": \""+getSerialNum()+"\"}";
      client.println("Content-Length: " + String(content.length()));
      client.println("Content-Type: application/json");
      client.println();
      client.println(content);
      // client.println("POST /api/join/ HTTP/1.1\nHost: 192.168.123.103:8080\nAccept: */*\nContent-Length: "+String(content.length())+"\nContent-Type: application/json\n\n"+content);

      // Parse JSON -> set TIMELAPSE_PERIOD
      delay(100);
      while(client.connected()){
        if(client.available()){
          char c = client.read();
          ringbuf.push(c);
          if (ringbuf.endsWith("od\": ")){
            String period = "";
            do{
              c = client.read();
              ringbuf.push(c);
              if (c != '}') period += c;
            } while(!ringbuf.endsWith("}"));

            TIMELAPSE_PERIOD = period.toInt();
            // Serial.print("period: ");
            // Serial.println(TIMELAPSE_PERIOD);
            suc = true;
          }
        }
      }
    }
  }
  // measure sensors, and send to server
  measure();
  delay(5000);
  // take pic, and send to server
  snapShot();
}



// ***********************LOOP******************************
void loop() {
  /*
  while (client.available()) {
    char c = client.read();
    Serial.write(c);
  }*/

  // measure period : 1h
  // measure sensor & send data to server (api/measure/)
  unsigned long l2 = millis();
  if (l2-l1 >= interval){
    measure();
    l1 = l2;
  }

  // timelapse period : TIMELAPSE_PERIOD
  // took picture & send data to server (api/uploadimages/)
  unsigned long l4 = millis();
  if (l4-l3 >= (unsigned long)TIMELAPSE_PERIOD*1000*60*60){
    snapShot();
    l3 = l4;
  }
}



// **************************Functions***********************************
int getLight(){     // 조도값 (0~100%) 반환
  int cdsvalue = analogRead(A3);
  return map(cdsvalue, 0, 1023, 100, 0);
}

float getTemp(){    // 섭씨온도 반환
  return dht.readTemperature();
}

float getHumi(){    // 습도 반환
  return dht.readHumidity();
}

int getWatery(){    // 토양수분 반환
  return (1023-analogRead(A1))/10;
}


bool cameraInit(){  // Initialize Camera module
  cam.begin(BaudRate_19200);
  char *reply = cam.getVersion();
  if (reply == 0) {
    Serial.println("Failed to get version");
    return false;
  }
  else {
    /*
    Serial.println("version:");
    Serial.println("-----------------");
    Serial.println(reply);
    Serial.println("-----------------");*/
    return true;
  }
}


void snapShot(){
  // 카메라 모듈 초기화
  while(false == cameraInit()){
    // Serial.println("camera init error...");
    // Serial.println("retry in 3 sec...");
    delay(3000);
  }
  // Serial.println("camera Init...OK");

  cam.resumeVideo();
  cam.setImageSize(VC0706_640x480);
  // Serial.println("Snap in 1 sec");
  delay(1000);
  if (! cam.takePicture()){
    Serial.println("Failed to snap!");
    reset();
  }
  else{
    // Serial.println("Picture taken!");
  }
  uint16_t jpglen = cam.getFrameLength();   // size of jpg
  /* print img size
  Serial.print("img size: ");
  Serial.print(jpglen, DEC);
  Serial.println(" byte");*/
  cam.getPicture(jpglen);
  uint8_t *buffer;  // 0~255 1byte unsigned int 

  // 와이파이 모듈 초기화
  WiFi.disconnect();
  delay(500);
  status = WL_IDLE_STATUS;
  WiFiEspClient client; // Initialize the Ethernet client object
  Serial1.begin(9600);
  WiFi.init(&Serial1);  // initialize ESP module
  if (WiFi.status() == WL_NO_SHIELD) {
    // Serial.println("WiFi shield not present");
    // reboot system
    reset();
  }
  while (status != WL_CONNECTED) {
    // Serial.print("connect to WPA SSID: ");
    // Serial.println(ssid);
    status = WiFi.begin(ssid, pass);
  }
  // Serial.println("connected");

  // 사진 읽어서 전송
  RingBuffer ringbuf(8);
  int count = 1;
  while(jpglen != 0){
    uint8_t bytesToRead = min(64, jpglen);  // 64바이트씩 읽기
    buffer = cam.readPicture(bytesToRead);
    String contextBuf = "{\"plant_id\": \""+getSerialNum()+"\", \"data\": \"";
    //String contextBuf = "{\"plant_id\": \"testSerial1\", \"data\": \"";
    for(int i = 0; i<(int)bytesToRead; i++){
      // String에 더해서 context 만들고 전송
      char buf[4] = {0,};
      sprintf(buf, "%02X", buffer[i]);
      contextBuf += String(buf);
      // Serial.print(buf);
    }
    contextBuf += "\"}";

    // 마지막 전송이면 카운트 0으로 보내기
    if (jpglen<=64){
      count = 0;
    }

    // api/uploadimages 전송 부분
    Serial1.listen();
    // Serial.println(contextBuf);
    int success = 0;
    int try_count = 0;
    while(success != 1){  // 전송 실패하면 다시 보내기
      client.flush();
      client.stop();
      if (client.connect(server, 8080)) {
        ringbuf.init();

        client.print("POST /api/uploadimages/");
        client.print(count,DEC);
        client.println("/ HTTP/1.1");
      
        client.println(("Host: 43.200.105.74:8080"));
        client.println("Accept: */*");

        client.print("Content-Length: ");
        client.println(contextBuf.length(), DEC);

        client.println("Content-Type: application/json");
        client.println();
        client.println(contextBuf);

        success = 1;
      }
      try_count++;
      if (try_count>50){
        // Critical Error! -> Reboot System
        reset();
      }
    }
    jpglen -= bytesToRead;
    count++;
  }

  // Serial.println();
  // Serial.println("Done!");
  client.flush();
  client.stop();
  cam.resumeVideo();
}


void measure(){ // 센서 측정 & 서버로 전송
  client.flush();
  client.stop();
  String context = "{\"plant_id\": \""+getSerialNum()+"\",\"light\": \""+String(getLight())+"\","+
  //String context = "{\"plant_id\": \"testSerial1\", \"light\": \""+String(getLight())+"\","+
                    "\"temp\": \""+String(getTemp())+"\","+
                    "\"humi\": \""+String(getHumi())+"\","+
                    "\"watery\": \""+String(getWatery())+"\",\"ph\": \"7.3\"}";
  // API measure
  if (client.connect(server, 8080)) {
    client.println("POST /api/measure/ HTTP/1.1");
    client.println("Host: "+String(server)+":8080");
    client.println("Accept: */*");

    client.println("Content-Length: " + String(context.length()));
    client.println("Content-Type: application/json");
    client.println();
    client.println(context);
    // client.println("POST /api/join/ HTTP/1.1\nHost: 192.168.123.103:8080\nAccept: */*\nContent-Length: "+String(content.length())+"\nContent-Type: application/json\n\n"+content);

    delay(500);
    client.flush();
    client.stop();
  }
}


void printWifiStatus(){
  // print the SSID of the network you're attached to
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address
  IPAddress ip = WiFi.localIP();
  Serial.print("IP: ");
  Serial.println(ip);

  // print the received signal strength
  long rssi = WiFi.RSSI();
  Serial.print("RSSI:");
  Serial.print(rssi);
  Serial.println(" dBm");
}


String getSerialNum(){
  String serialNum = "";
  for (int i = 0; i < 6; i++) {
    char buf[4];
    sprintf(buf, "%02X", boot_signature_byte_get(i));
    // serialNum += String(boot_signature_byte_get(i), HEX);
    serialNum += String(buf);
  }
  return serialNum;
}