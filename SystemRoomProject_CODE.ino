#include <WiFi.h> //Wifiye baglanma islemleri.(ESP32)
#include "time.h" //Ntpserver icin eklendi.
#include <ESP32Servo.h> //Esp32 servo motor kutuphanesi
#include <WiFiClientSecure.h> //Wifi Client olusturma. PUB/SUB islemleri,mqtt sunucusuna baglanma ve Telegram icin gereklidir.
#include <UniversalTelegramBot.h> //https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot/archive/master.zip - Telegram kutuphanesi
#include <ArduinoJson.h> //https://github.com/bblanchon/ArduinoJson - JSON kutuphanesi
#include "DHT.h" //Sicaklik ve nem sensuru kutuphanesi.
#include <PubSubClient.h> // Pub/Sub kutuphanesi(MQTT).

#define DHTPIN 33
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

float nem=0;
float sicaklik=0;
int gazDeger=0;
int sayac=0; //Sensor esik degerleri asilma sayisini tutma amacli bir sayac degiskeni
bool alarmDurum=false; //Baslangicta Alarm Kapali
int x2=1; // loop fonksiyonu icinde esik deger kontrolü icin kullanilan ara degisken

const int buzzerPin = 23;
const int ledPin = 19;

const char* ssid       = "TurkTelekom_ZXUMT";
const char* password   = "f707730997990";
const char* mqtt_server = "192.168.1.150"; //MQTT server ip adresi

// pub/sub ve mqtt için client olusturma
WiFiClient espClient;
PubSubClient client(espClient);

// sensorlerin degerlerini tutmak icin olusturulan char dizileri
#define MSG_BUFFER_SIZE  (50)
#define MSG_BUFFER_SIZE_JSON  (1000)
char msg_pir[MSG_BUFFER_SIZE];
char msg_kapidurumu[MSG_BUFFER_SIZE];
char msg_zamanBilgisi[MSG_BUFFER_SIZE];
char msg_telegramKapiKomut[MSG_BUFFER_SIZE];

char msg_nem[MSG_BUFFER_SIZE];
char msg_sicaklik[MSG_BUFFER_SIZE];
char msg_gazDeger[MSG_BUFFER_SIZE];
char msg_alarm[MSG_BUFFER_SIZE];

// JSON data olusturma
StaticJsonDocument<1000> JSON_Data;
char buffer[1000];
char msg_json_data[MSG_BUFFER_SIZE_JSON];

unsigned long lastMsg = 0; //milis fonksiyonu icin olusturuldu

// Telegram BOT için gereken token
#define BOTtoken "1693141572:AAFrFlls6qgqA1LZD_BK-3wNSDz_ZPCsZa0"  // BotFather'dan aldığınız TOKEN

// IDBOT uzerinden /getid mesaji ile alinan ID
#define CHAT_ID "665296804"

//telegram bot baglantisi
WiFiClientSecure client_tlgrm;
UniversalTelegramBot bot(BOTtoken, client_tlgrm);

int botIstekGecikmesi = 1000; //Telegramdan 1 sn'de bir mesaj alindi mi diye kontrol ediliyor.
unsigned long botSonCalismaZamani; //milis fonksiyonu icin olusturuldu.

int BilgiGecikmesi = 3000; // sicaklik,nem,gaz sensorleri her 3 sn'de bir okunuyor.
unsigned long bilgiSonCalismaZamani;

bool kapiDurum=false; // Baslangicta kapi komutu false
bool servoDurum=false; // Baslangicta kapi kapali

//ntpServer ayarlari
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 10800; //saat farkı - GMT+3 (saniye cinsinden) - 3*60*60
const int   daylightOffset_sec = 0; //yaz saati uygulaması
char tamTarih_char[30];
String tamTarih_str;
char saat_char[3];
String saat_str;

Servo servoNesnesi;  //* servo motor nesnesi yaratildi.
const int pirPin = 35;
const int kapiButon=34; //Kapi acma butonu pini
int pir_deger = 0; //Baslangicta PIR 0

void reconnect() { // MQTT sunucusuna baglanma fonksiyonu
  // MQTT sunucusuna baglanana(connected) kadar while'den cikilmaz.
  while (!client.connected()) {
    //client'in mqtt servere baglanma bilgileri
    Serial.print("MQTT Sunucusuna Bağlanılıyor..");
    // Random client ID olusturuluyor.
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // MQTT sunucusuna baglanmaya calisiliyor.
    if (client.connect(clientId.c_str())) {
      Serial.println(" - MQTT Sunucusuna bağlanıldı");
    } 
    else {
      Serial.print("MQTT Sunucusuna bağlanılamıyor, Durum: ");
      Serial.print(client.state());
      Serial.println("5 Saniye İçerisinde Yeniden Bağlanılacak");
      delay(5000);
    }
  }
}

//
void printLocalTime()
{
  struct tm timeinfo; //zamanla ilgili tum ayrintilari (dakika, saniye, saat, vb.) iceren timeinfo adli bir zaman yapisi (struct tm) olusturuluyor.
  if(!getLocalTime(&timeinfo)){ //tarih ve saat bilgileri alinip timeinfo yapisina kaydedidiliyor. zaman bilgisi alınamazsa serial monitöre hata yazdırılır.
    Serial.println("Zaman Bilgisi Alınamadı"); 
  }
  
  strftime(tamTarih_char,30, "%x - %X", &timeinfo);
  tamTarih_str=String(tamTarih_char);
  strftime(saat_char,3, "%H", &timeinfo); //Saat bilgisi tarih nesnesinden char dizisine çevriliyor
  saat_str=String(saat_char); //char dizisi stringe çevriliyor. (00,01,..,24)
}

//Telegramdan gelen komutlari Yonetme islemleri
void yeniMesajlariYonet(int kacYeniMesaj) {
  for (int i=0; i<kacYeniMesaj; i++) {
    // talebi yapan kullanıcının chatid'si
    String chat_id = String(bot.messages[i].chat_id);
    //eğer farklı bir kullanıcıdan komut gelirse izin verilmez.
    if (chat_id != CHAT_ID){
      bot.sendMessage(chat_id, "Yetkisiz Kullanıcı", "");
      continue;
    }
    
    // Alınan komut mesajını serial monitore yazdiralim.
    String text = bot.messages[i].text;
    Serial.println(text);

    String from_name = bot.messages[i].from_name; //telegram kullanici adi

    if (text == "/bilgial") {
      String bilgi = "Merhaba, " + from_name + ".\n";
      bilgi += "Kullanabilecek Komutlar:\n\n";
      bilgi += "/kapi_ac ile Kapı açma komutu gönderilir \n";
      bilgi += "/kapi_kapat ile Kapı kapatma komutu gönderilir \n";
      bilgi += "/kapi_durum ile Kapının açık yada kapalı olma bilgisi gösterilir. \n";
      bot.sendMessage(chat_id, bilgi, "");
    }

    if (text == "/kapi_ac") {
      bot.sendMessage(chat_id, "Kapının açılması için gerekli olan komut alındı!", "");
      kapiDurum=true;
    }
    
    if (text == "/kapi_kapat") {
      bot.sendMessage(chat_id, "Kapının kapanması için gerekli olan komut alındı!", "");
      kapiDurum=false;
    }
    
    if (text == "/kapi_durum") {
      if (kapiDurum)
        bot.sendMessage(chat_id, "Kapı durumu: AÇIK", "");
      else
        bot.sendMessage(chat_id, "Kapı durumu: KAPALI", "");
    }
  }
}

//servo motoru loop fonksiyonunda kontrol etme amacli degiskenler
int x=1;
int y=0;

void IRAM_ATTR Kapi() {
  printLocalTime(); //serial monitörde yazılması için fonksiyon çagrildi
  if (pir_deger == HIGH && kapiDurum==true && (saat_str.toInt()<=20 && saat_str.toInt()>=07)) {
      servoDurum=true;
      Serial.println("Kapı açıldı");
  }
  else if(kapiDurum==true && servoDurum==false)
    Serial.println("Telegram'dan Kapı Açılma komutu alındı, fakat pır sensöründe hareket algılamadı veya saat aralıkları dışındasınız.");
  else if(kapiDurum==true && servoDurum==true)
    Serial.println("Kapı zaten açık, kapıyı kapatmak için lütfen telegram üzerinden /kapi_kapat komutunu gönderin ve butona basın");
  else {
      if(servoDurum==false) Serial.println("Kapı zaten kapalı");
      else {
      servoDurum=false;
      y=1;
      Serial.println("Kapı kapandı");
          } 
      }
}

void setup() {
  Serial.begin(115200);
  pinMode(pirPin, INPUT);
  pinMode(kapiButon,INPUT);
  pinMode(buzzerPin, OUTPUT);
  pinMode(ledPin, OUTPUT);

  dht.begin();
  servoNesnesi.attach(32);

  //Wifi ayarlari
  Serial.printf("Kablosuz Ağa Bağlanılıyor: %s ", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  client_tlgrm.setCACert(TELEGRAM_CERTIFICATE_ROOT);
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
  Serial.println("");
  Serial.println("Ağa Bağlanıldı");
  Serial.print("IP adresi: ");
  Serial.println(WiFi.localIP());
  
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer); //daha önce belirlenen ayarlar ile zaman ayarlanıyor.

  // Butona basildiginda interrupt(kesme) olusturuluyor ve Herhangi bir anda kodu durdurup çalısiyor. Kesme Modunu RISING(0 -> 1) olarak ayarlıyoruz.
  attachInterrupt(digitalPinToInterrupt(kapiButon), Kapi, RISING); //Butona basildiginda IRAM_ATTR Kapi fonksiyonu calisir.

  //sistemin çalısmaya basladigini belirtmek icin Telegrama Bilgi gonderiliyor.
  printLocalTime(); //zaman bilgisi alınıyor.
  bot.sendMessage(CHAT_ID, "Sistem Başlatıldı. Tarih ve Saat: " + tamTarih_str, "");
  servoNesnesi.write(0); // Sistem başlangıcında kapı kapalı

  client.setServer(mqtt_server, 1883); //mqtt server baglanti bilgileri set ediliyor.
}


void loop(){
  
  if (millis() > bilgiSonCalismaZamani + BilgiGecikmesi)  { // 3 saniyede bir burasi calisir.
    nem = dht.readHumidity();
    sicaklik = dht.readTemperature();
    gazDeger=analogRead(36);
    gazDeger= map(gazDeger,0,4095,0,1023); //0-4095 araligi 0-1023 araligina map ediliyor.
    
    snprintf(msg_nem, MSG_BUFFER_SIZE, "%s", String(nem));
    snprintf(msg_sicaklik, MSG_BUFFER_SIZE, "%s", String(sicaklik));
    snprintf(msg_gazDeger, MSG_BUFFER_SIZE, "%s", String(gazDeger));
    snprintf(msg_alarm, MSG_BUFFER_SIZE, "%s", String(alarmDurum));
    client.publish("nemSensor", msg_nem);
    client.publish("sicaklikSensor", msg_sicaklik);
    client.publish("gazSensor", msg_gazDeger);

    JSON_Data["sicaklikSensor"] = msg_sicaklik;
    JSON_Data["nemSensor"] = msg_nem;
    JSON_Data["gazSensor"] = msg_gazDeger;
    
    if(nem>51.0 || sicaklik>40.0 || gazDeger>400){ //esik deger kontrolü
       if(sayac!=3){
        Serial.println("Sensör Degerleri asildi! - " +String(sayac + 1));
        sayac++;
       }
       else if(sayac==3){
          if(x2==1) {
            printLocalTime();
            Serial.println("Alarm baslatildi!");
            digitalWrite(ledPin, HIGH);
            bot.sendMessage(CHAT_ID, "Alarm " + tamTarih_str + " tarihinde sensör değerleri aşıldığı için başlatıldı.", "");
            alarmDurum=true;
            snprintf(msg_alarm, MSG_BUFFER_SIZE, "%s", String(alarmDurum));
            client.publish("alarmDurumu", msg_alarm); 
            x2++;
          }        
          beep(250);
       }
    }

    else{
    if(x2==2) {
      Serial.println("Sensör Degerleri normale dondugunden alarm durduruldu");
      Serial.println("");
      digitalWrite(ledPin, LOW);
      bot.sendMessage(CHAT_ID, "Alarm " + tamTarih_str + " tarihinde sensör değerleri normale döndüğü için kapatıldı.", "");
      alarmDurum=false;
      snprintf(msg_alarm, MSG_BUFFER_SIZE, "%s", String(alarmDurum));
      client.publish("alarmDurumu", msg_alarm);
    }


    Serial.print(F("Nem: "));
    Serial.print(nem);
    Serial.print(F("%  Sıcaklık: "));
    Serial.print(sicaklik);
    Serial.print(F("°C "));
    Serial.println(F(""));
    Serial.print("Gaz ve Duman: ");
    Serial.print(gazDeger);
    Serial.println(F(""));
    sayac=0;
    x2=1;
    }
    JSON_Data["alarmDurumu"] = msg_alarm;
    bilgiSonCalismaZamani = millis();
  }

  //millis fonksiyonunu kullanarak 1 sn'de bir Telegramdan mesaj gelip gelmedigini kontrol ediliyor.
  if (millis() > botSonCalismaZamani + botIstekGecikmesi)  {
    int kacYeniMesaj = bot.getUpdates(bot.last_message_received + 1);

    while(kacYeniMesaj) { //yeni mesaj gelmişse serial monitore yazdirilir.
      Serial.print("Mesaj alındı: ");
      yeniMesajlariYonet(kacYeniMesaj);
      kacYeniMesaj = bot.getUpdates(bot.last_message_received + 1);
    }
    botSonCalismaZamani = millis();
  }

  if(servoDurum==true) {
     if(x==1){
        servoNesnesi.write(180);
        String from_name = bot.messages[0].from_name;
        bot.sendMessage(CHAT_ID, "Kapı " + tamTarih_str + " tarihinde " + from_name + " kullanıcısı tarafından açıldı.", "");
        x++;
        y=1;
      }
  }
  else{
    if(y==1){
       servoNesnesi.write(0);
       String from_name = bot.messages[0].from_name;
       bot.sendMessage(CHAT_ID, "Kapı " + tamTarih_str + " tarihinde " + from_name + " kullanıcısı tarafından kapatıldı.", "");
       y++;
       x=1; 
    }
 }

   if (!client.connected()) { // client mqtt servere connected değilse reconnect çalışır
        reconnect();
   }
   client.loop(); //mqtt loop

  //pır,telegramKapıKomut,kapı durumu,zaman bilgisi 1 saniyede bir  alınıyor.
  unsigned long now = millis();
  if (now - lastMsg > 1000) {
    lastMsg = now;
    printLocalTime();
    pir_deger = digitalRead(pirPin);
    snprintf(msg_pir, MSG_BUFFER_SIZE, "%ld", pir_deger);
    snprintf(msg_kapidurumu, MSG_BUFFER_SIZE, "%s", String(servoDurum));
    snprintf(msg_telegramKapiKomut, MSG_BUFFER_SIZE, "%s", String(kapiDurum));
    snprintf(msg_zamanBilgisi, MSG_BUFFER_SIZE, "%s", tamTarih_char);

    JSON_Data["pırDurumu"] = msg_pir;
    JSON_Data["telegramKapiKomut"] = msg_telegramKapiKomut;
    JSON_Data["kapiDurumu"] = msg_kapidurumu;

    //json data stringe cevriliyor.
    serializeJson(JSON_Data, buffer);
    snprintf(msg_json_data, MSG_BUFFER_SIZE_JSON, "%s", buffer);

    client.publish("kapiDurumu", msg_kapidurumu);
    client.publish("pırDurumu", msg_pir);
    client.publish("telegramKapiKomut", msg_telegramKapiKomut);
    client.publish("zamanBilgisi", msg_zamanBilgisi);
    
    client.publish("json_pub", msg_json_data);

  }
}

void beep(unsigned char delayms){
digitalWrite(buzzerPin, HIGH); // Buzzer Ses Verir
delay(delayms); // Belirlilen MS cinsinden bekletme
digitalWrite(buzzerPin, LOW); // Buzzer Sesi Kapatır
delay(delayms); // Belirlilen MS cinsinden bekletme
}
