#include "BluetoothSerial.h"
#include "DHT.h"
#include <OneWire.h>
#include <DallasTemperature.h>
#include <TinyGPSPlus.h>
#include <Adafruit_BMP085.h>

Adafruit_BMP085 bmp;
TinyGPSPlus gps;

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif
BluetoothSerial SerialBT;
#define DHTPIN 13
#define DHTTYPE DHT11

const int PINO_INT = 5;
// Variável compartilhada com a interrupção
volatile bool flagInterrupcao = false;
// Opcional: para "debounce" por software
volatile unsigned long ultimoTempoInt = 0;
const unsigned long debounceMs = 500;

DHT dht(DHTPIN, DHTTYPE);

float h_bmp;
bool bmp_ok;
unsigned int pluv=0;
char lat[32], lng[32];
char Date[32], Time[32];
int dia_anterior = 99; 

const int oneWireBus = 4; //DS18B20 conectado ao GPIO4 
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);

const int signalPin = 2;  // anemometro
unsigned long lastTime = 0;   
unsigned int frequency = 0;

#define RXD2 16
#define TXD2 17
#define GPS_BAUD 9600

HardwareSerial gpsSerial(2);

/**************************************************************************************************************************************************/

// Rotina de interrupção (ISR)
void IRAM_ATTR trataInterrupcao() {
  unsigned long agora = millis();
  
  // Simples "debounce": ignora interrupções muito próximas
  if (agora - ultimoTempoInt > debounceMs) {
    flagInterrupcao = true;      // Sinaliza para o loop principal
    ultimoTempoInt = agora;
    pluv++; 
  }
}

/**************************************************************************************************************************************************/

float le_temperatura(){
  sensors.requestTemperatures(); 
  float temp = sensors.getTempCByIndex(0);
  return(temp);
}

/**************************************************************************************************************************************************/

unsigned long le_anemometro(){
  unsigned long currentTime = millis();
  unsigned long lastTime=currentTime;
  bool lastSensorState = LOW; 
  unsigned long pulseCount=0; 
  float vel;
  while(currentTime-lastTime <= 1000){
    currentTime = millis();
    bool sensorState = digitalRead(signalPin);
    if (sensorState == HIGH){
      if (lastSensorState == LOW){
        pulseCount++;
      }
    }
    lastSensorState = sensorState;
  }
  vel=pulseCount*0.08;
  return(vel);
}

/**************************************************************************************************************************************************/

float le_pressao(){
  if(bmp_ok==true) {
    float t_bmp = bmp.readTemperature();
    float press = bmp.readPressure();
    return(press);
  }
  return 0.0;
}

/**************************************************************************************************************************************************/

float le_umidade(){
  float t_dht11 = dht.readTemperature();
  float u_dht11 = dht.readHumidity();
  return(u_dht11);
}

//******************************************************************************************

void setup() {
  // put your setup code here, to run once:
  Serial.begin(38400);
  sensors.begin();
  dht.begin();
  pinMode(signalPin, INPUT); // Configura o pino como entrada
  bmp_ok=false;
  if (bmp.begin()) {
	  Serial.println("valid BMP085/BMP180 sensor");
	  bmp_ok=true;
  }
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, RXD2, TXD2);
  SerialBT.begin("ESP32test"); //Bluetooth device name
  //Serial.println("The device started, now you can pair it with bluetooth!");
  pinMode(PINO_INT, INPUT_PULLUP); 
  // Ajuste conforme seu circuito: INPUT, INPUT_PULLUP, etc.

  // Interrupção no pino GPIO 4, borda de descida (FALLING)
  attachInterrupt(
    digitalPinToInterrupt(PINO_INT),
    trataInterrupcao,
    FALLING
  );

}

/**************************************************************************************************************************************************/



static void captureFloat(float val, bool valid, int len, int prec, char *v)
{
  if (!valid)
  {
    for(int i =0; i < (len-1); i++){
        v[i] = '*';
    }
    v[len-1] = '\0';

  }
  else
    snprintf(v, 32, "%*.*f", len, prec, val);

  smartDelay(0);
}

/**************************************************************************************************************************************************/

static void smartDelay(unsigned long ms)
{
  unsigned long start = millis();
  do 
  {
    while (gpsSerial.available())
      gps.encode(gpsSerial.read());
  } while (millis() - start < ms);
}

/**************************************************************************************************************************************************/

static void captureDateTime(TinyGPSDate &d, TinyGPSTime &t)
{
  if (!d.isValid())
  {
    for(int i = 0; i < 10; i++){
      Date[i] = '*';
      Time[i] = '*';
    }
      Date[10] = ' ';
      Time[10] = ' ';
      Date[11] = '\0';
      Time[11] = '\0';
      
  }
  else
  {
    sprintf(Date, "%02d/%02d/%02d ", d.month(), d.day(), d.year());
    sprintf(Time, "%02d:%02d:%02d ", t.hour(), t.minute(), t.second());
    
  }
}
/**************************************************************************************************************************************************/


void le_gps(){
  captureDateTime(gps.date, gps.time);
  captureFloat(gps.location.lat(), gps.location.isValid(), 11, 6 , lat);
  captureFloat(gps.location.lng(), gps.location.isValid(), 12, 6, lng);
}


/**************************************************************************************************************************************************/

float le_pluv(){
  return(0);
}


/**************************************************************************************************************************************************/

float le_direcao(){
  return(0);
}

/**************************************************************************************************************************************************/

float le_radiacao(){
  return(0);
}

/**************************************************************************************************************************************************/

void loop() {
 
  le_gps();

  int dia_atual = gps.date.day(); 
    if (dia_anterior != dia_atual) {
        pluv = 0;                     // zera a chuva para o dia seguinte
        dia_anterior = dia_atual;     
    }



  float temperatura=le_temperatura();
  float umidade=le_umidade();
  float pressao=le_pressao();
  float velocidade=le_anemometro();
  float direcao=le_direcao();
  float radiacao=le_radiacao();

  Serial.print(Date);
  Serial.print("  ");
  SerialBT.print(Date);
  SerialBT.print("  ");
 

  Serial.print(Time);
  Serial.print(" ");
  SerialBT.print(Time);
  SerialBT.print(" ");
 
  Serial.print(lat);
  Serial.print("  ");
  SerialBT.print(lat);
  SerialBT.print("  ");

  Serial.print(lng);
  Serial.print("  ");
  SerialBT.print(lng);
  SerialBT.print("  ");


  Serial.print(temperatura);
  Serial.print(" ");
  SerialBT.print(temperatura);
  SerialBT.print(" ");

  Serial.print(umidade);
  Serial.print(" ");
  SerialBT.print(umidade);
  SerialBT.print(" ");

  if (bmp_ok==true)
  {  Serial.print(pressao);
     Serial.print(" ");
     SerialBT.print(pressao);
     SerialBT.print(" ");
  }
  else
  {
     Serial.print("0000.00");
     Serial.print(" ");
     SerialBT.print("0000.00");
     SerialBT.print(" ");
  }

  Serial.print(pluv);
  Serial.print(" ");
  SerialBT.print(pluv);
  SerialBT.print(" ");

  Serial.print(radiacao);
  Serial.print(" ");
  SerialBT.print(radiacao);
  SerialBT.print(" ");


  Serial.print (velocidade);
  Serial.print(" ");
  SerialBT.print (velocidade);
  SerialBT.print(" ");

  Serial.println(direcao);
  SerialBT.println(direcao);
  delay(2000);

}
