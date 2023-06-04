#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <PN532_I2C.h>
#include <PN532.h>
//CONEXION WIFI
#define SSID "red_wifi"
#define PSK "contraseña_wifi"
//DIRECCION DE SERVIDOR (url de API)
#define URL "https://identitytoolkit.googleapis.com/v1/"
//Usuario y contraseña para login
#define USER "daniielatrejo24@gmail.com"
#define PASS  "123456"

//alto y ancho de pantalla
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
//pines de conexion I2C en el ESP32 (No deben cabiarese)
#define I2C_SDA 21
#define I2C_SCL 22
//Led integrado(azul) del ESP32
#define LED_ST 2

//variable ID unico de la tarjeta RF MiFare
//la variable de llama tagId32 y como es de tipo union, se puede usar tagId32.array para obtener sus bytes o
//tagId32.integer para obtener su valor entero
union ArrayToInteger {
  byte array[4];
  uint32_t integer;
} tagId32;
//Permite conectar al mismo cable dos dispositivos
TwoWire WireI2C = TwoWire(0);
//permite controlar la pantalla OLED
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &WireI2C, -1);
//Permite controlar el lector NFC/RFID
PN532_I2C pn532_i2c(WireI2C);
PN532 nfc (pn532_i2c);

//buffer temporal para leer el ID de las tarjetas
boolean success;
uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
uint8_t uidLength;

WiFiMulti wifiMulti;
//variables de sesion
String jwt;
bool sesion;

void setup(void) {
  //Inicializa serial y manda imprimir un mensaje
  Serial.begin(115200);
  Serial.println("Setup....");
  //inicializa led integrado y lo apaga
  pinMode(LED_ST, OUTPUT);
  digitalWrite(LED_ST, LOW);
  
  //inicializa cables I2C de pantalla y NFC/RFID
  WireI2C.begin(I2C_SDA, I2C_SCL, 10000);
  //inicializa pantalla
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 fallo conexión"));
    for (;;);
  }
  delay(1000);
  //imprime mensaje en pantalla
  printLCD("Conectando a red....");
  //conecta a wifi
  wifiMulti.addAP(SSID, PSK);
  sesion = false;
  while(wifiMulti.run() != WL_CONNECTED) ;
  //imprime mensaje en pantala de iniciando e inicializa lector NFC/RFID
  printLCD("Iniciando....");
  nfc.begin();
  //Comprueba si se inicializo correctamente el lector NFC/RFID
  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
    Serial.println("Didn't find PN53x board");
    printLCD("ERROR RF");
    delay(1000);
    while (1) ;
  }
  //Se configura el lector NFC/RFID para tarjetas MiFare clasicas
  nfc.setPassiveActivationRetries(0xFF);
  nfc.SAMConfig();
  //Enciende led integrado e imprime mensaje de BIENVENIDO en pantalla y en puerto serial
  digitalWrite(LED_ST, HIGH);
  printLCD("BIENVENIDO");
  Serial.println("Sistema Inicializado");
}

void loop() {
  readNFC();
}

void readNFC() {
  //Lee si hay tarjeta RFID, en tal caso devuelve un true, si no hay tarjeta es un false
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength, 50);
  printLCD("BIENVENIDO");
  //si leyo una tarjeta
  if (success) {
    Serial.println(uidLength);
    //copia el arreglo de bytes de la tarjeta leida a la variable tagId32
    //esto permite que se convirta automaticamente el arreglo de bytes a un valor entero
    tagId32.array[0] = uid[0];
    tagId32.array[1] = uid[1];
    tagId32.array[2] = uid[2];
    tagId32.array[3] = uid[3];
    //imprime el ID de la tarjeta en serial y en pantalla OLED
    Serial.println(tagId32.integer);
    printLCD("ID LEIDO");
    printLCD(String(tagId32.integer), 2);
    //Si esta conectado a WiFi ejecuta login para iniciar sesion 
    //y pay para madar el ID de la tarjeta al servidor
    //sino imprime en pantalla un mensaje de sin red
    if((wifiMulti.run() == WL_CONNECTED)) {
      if (!sesion) login();
      consultar();
    }else{
      printLCD("SIN RED...");
      delay(5000);
    }
  }
  delay(500);
}
//Funcion para imprimir mensajes en pantalla
void printLCD(String x){
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.clearDisplay();
  printLCD(x, 0);
}
//funcion para imprimier mensajes en una segunda linea en pantalla
void printLCD(String x, int linea){
  display.setCursor(0, linea * 16 + 2);
  display.println(x);
  display.display();
}

//funcion para hacer login y obtener el token JWT del sevidor
void login(){
  HTTPClient http;
    //url de la api o servicio rest del login
    //en arduino las constantes se concatenan sin ningun operador
    //por ejemplo aqui se hace un get a PHP "http://172.16.253.30/smartpay/login.php?user=admin&pass=123"
    http.begin(URL "accounts:signInWithPassword?key=AIzaSyBLwOeyYbtfaa6N8Jp5vWkRff70IxdFX8M");
    //  int httpCode = http.GET();
    //En el caso de una Web API de C# debe mandarse como JSON
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.POST("{\"email\":\"" USER "\",\"password\":\"" PASS "\",\"returnSecureToken\":true}");
    
    Serial.println("Haciendo login");
    if(httpCode > 0) {
      if(httpCode == HTTP_CODE_OK) {
          //payload contiene lo que el server haya enviado, generalmente un json
          String payload = http.getString();
          Serial.println(payload);
          //se extrae del json el token JWT mediante la funcion buscarJson()
          jwt = buscarJson("idToken", payload);
          Serial.println(jwt);
          sesion = true;
      }
      else if(httpCode == 401) {
        //si el servidor contesta un codigo HTTP 401 significa que no tiene autorizacion o permiso de acceder a server
        sesion = false;
      }
    }
    http.end();
}

//funcion para enviar el ID de la tarjeta al server
void consultar(){
  HTTPClient http;
  //url a donde se quiera mandar el id
  http.begin("https://firestore.googleapis.com/v1/projects/paywallet-e4c84/databases/(default)/documents:runQuery");
  http.addHeader("Content-Type", "application/json");
  //http.addHeader("Authorization", "Bearer " + jwt);
  //envia un json con el id de la tarjeta similar a {"tagid":"0000000000"}
  int httpCode = http.POST("{\"structuredQuery\": {\"from\": [{\"collectionId\": \"usuarios\"}],\"where\": {\"fieldFilter\": {\"field\": {\"fieldPath\": \"rfid\"},\"op\": \"EQUAL\",\"value\": {\"stringValue\": \""+String(tagId32.integer)+"\"}}}}}");
  
  if(httpCode > 0) {
      if(httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println(payload);
        Serial.println("Registro exitoso");

        // Al cambiar el campo, no se puede leer el valor
        // Se dejó de momento el campo noControl para poder leer el valor y comprobar la conexión
        int i,f;
        String control;
        i = payload.indexOf("noControl");
        if(i > 0){
          i = payload.indexOf("stringValue", i);
          i = payload.indexOf(":", i);
          i = payload.indexOf("\"", i) + 1;
          f = payload.indexOf("\"", i);
          control = payload.substring(i, f);
          Serial.println(control);
          printLCD(control);
        } else {
          Serial.println("NO EXISTE");
          printLCD("NO EXISTE");
        }
        
        delay(5000);
        return;
      }else{
        Serial.println("No se pudo cobrar " + httpCode);
      }
  }else{
    Serial.println("No hay conexion con servidor " + httpCode);
  }
  printLCD("ERROR!!");
  delay(5000);
 }
//permite buscar el valor de una clave en un json, por ejemplo
//en el json = {"control": "S22030001", "nombre": "JUAN"} 
//buscarJson("control", json) devoveria "S22030001"
String buscarJson(String clave, String json){
  int i,f;
  String valor;
  i = json.indexOf(clave);
  i = json.indexOf(":", i);
  i = json.indexOf("\"", i) + 1;
  f = json.indexOf("\"", i);
  return json.substring(i, f);
}