#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_Fingerprint.h>
#include <RTClib.h>
#include <SoftwareSerial.h>

// Pinos
#define LED_VERDE    6
#define LED_VERMELHO 7
#define BUZZER       8
SoftwareSerial serialSensor(2, 3);

LiquidCrystal_I2C lcd(0x27, 16, 2);
Adafruit_Fingerprint sensor = Adafruit_Fingerprint(&serialSensor);
RTC_DS1307 rtc;

struct Aluno {
  uint8_t id;
  char nome[16];        
  char ra[9];
};

Aluno alunos[] = {
  {1, "Yasmin Mamud",    "26000288"},
  {2, "Nicolas Lima",    "26000287"},
  {3, "Thais Sales",     "26000193"},
  {4, "Gustavo Staut",   "26000306"},
  {5, "Ewelyn Cristina", "26001449"},
  {6, "Vinicius Rugani", "26000211"},
  {7, "Matheus Bento", "26000291"}
};
uint8_t totalAlunos = sizeof(alunos) / sizeof(alunos[0]);

struct Registro {
  char    ra[9];
  char    data[7]; 
  uint8_t hora;
  uint8_t minuto;
};


#define MAX_REGISTROS 40
Registro chamada[MAX_REGISTROS];
uint8_t  totalRegistros = 0;
bool     jaEnviouHoje   = false;


unsigned long ultimoPisque = 0;
bool estadoLedVerde = false;
#define INTERVALO_PISQUE 500


Aluno* buscarAluno(uint8_t id) {
  for (uint8_t i = 0; i < totalAlunos; i++)
    if (alunos[i].id == id) return &alunos[i];
  return NULL;
}


void enviarChamada() {
  if (totalRegistros == 0) {
    Serial.println(F("[ENVIO] Sem registros."));
    return;
  }

  Serial.println(F("##JSON_START##"));
  Serial.print(F("{\"registros\":["));

  for (uint8_t i = 0; i < totalRegistros; i++) {
    Registro& r = chamada[i];
    Serial.print(F("{\"ra\":\""));  Serial.print(r.ra);
    Serial.print(F("\",\"data\":\"")); Serial.print(r.data);
    Serial.print(F("\",\"hora\":\""));
    Serial.print(r.hora);
    Serial.print(F(":"));
    if (r.minuto < 10) Serial.print(F("0"));
    Serial.print(r.minuto);
    Serial.print(F("\"}"));
    if (i < totalRegistros - 1) Serial.print(F(","));
  }

  Serial.println(F("]}"));
  Serial.println(F("##JSON_END##"));
  memset(chamada, 0, sizeof(chamada));

  totalRegistros = 0;
  jaEnviouHoje   = false;
  Serial.println(F("[ENVIO] OK. RAM liberada."));
}



void salvarRegistro(const char* ra, uint8_t hora, uint8_t minuto, const char* data) {

  
  if (totalRegistros >= MAX_REGISTROS) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(F("Mem. cheia!"));
    lcd.setCursor(0, 1); lcd.print(F("Enviando..."));
    Serial.println(F("##OVERFLOW##"));
    enviarChamada();
  }

  strncpy(chamada[totalRegistros].ra,   ra,   8); chamada[totalRegistros].ra[8]   = '\0';
  strncpy(chamada[totalRegistros].data, data, 6); chamada[totalRegistros].data[6] = '\0';
  chamada[totalRegistros].hora   = hora;
  chamada[totalRegistros].minuto = minuto;
  totalRegistros++;

  Serial.print(F("[REG] #")); Serial.print(totalRegistros);
  Serial.print(F(" RA:")); Serial.print(ra);
  Serial.print(F(" ")); Serial.print(hora);
  Serial.print(F(":")); if (minuto < 10) Serial.print(F("0"));
  Serial.println(minuto);
}


void verificarEnvio() {
  DateTime agora = rtc.now();

  if (agora.hour() == 0 && jaEnviouHoje)
    jaEnviouHoje = false;

  if (agora.hour() == 23 && agora.minute() == 0 && !jaEnviouHoje) {
    jaEnviouHoje = true;
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print(F("Enviando..."));
    enviarChamada();
    lcd.clear(); lcd.print(F("Sensor OK"));
  }
}


void acessoPermitido(Aluno* aluno) {
  DateTime agora = rtc.now();

  char dataStr[7];
  sprintf(dataStr, "%02d%02d%02d",
    agora.day(), agora.month(), agora.year() % 100);

  char linhaHora[17];
  sprintf(linhaHora, "%02d:%02d  %02d/%02d/%02d",
    agora.hour(), agora.minute(),
    agora.day(), agora.month(), agora.year() % 100);

  digitalWrite(LED_VERMELHO, LOW);
  digitalWrite(LED_VERDE, HIGH);

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(aluno->nome);
  lcd.setCursor(0, 1); lcd.print(linhaHora);

  digitalWrite(BUZZER, HIGH); delay(200); digitalWrite(BUZZER, LOW);

  Serial.print(F("[OK] ")); Serial.print(aluno->nome);
  Serial.print(F(" RA:")); Serial.println(aluno->ra);

  salvarRegistro(aluno->ra, agora.hour(), agora.minute(), dataStr);

  delay(3000);
  digitalWrite(LED_VERDE, LOW);
  lcd.clear(); lcd.print(F("Sensor OK"));
}

// =============================================
// ACESSO NEGADO
// =============================================
void acessoNegado() {
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, HIGH);

  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(F("Nao identificado"));
  lcd.setCursor(0, 1); lcd.print(F("Tente novamente"));

  Serial.println(F("[NEGADO] Digital desconhecida."));

  for (int i = 0; i < 2; i++) {
    digitalWrite(BUZZER, HIGH); delay(150);
    digitalWrite(BUZZER, LOW);  delay(100);
  }

  delay(2000);
  digitalWrite(LED_VERMELHO, LOW);
  lcd.clear(); lcd.print(F("Sensor OK"));
}

// =============================================
// LED PISCANTE
// =============================================
void piscarLedVerde() {
  unsigned long agora = millis();
  if (agora - ultimoPisque >= INTERVALO_PISQUE) {
    ultimoPisque   = agora;
    estadoLedVerde = !estadoLedVerde;
    digitalWrite(LED_VERDE, estadoLedVerde);
  }
}

// =============================================
// SETUP
// =============================================
void setup() {
  pinMode(LED_VERDE,    OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(BUZZER,       OUTPUT);

  lcd.init();
  lcd.backlight();
  lcd.print(F("Iniciando..."));

  Serial.begin(9600);
  Serial.println(F("=== CHAMADA ==="));
  Serial.println(F("L=listar E=enviar C=limpar"));
  delay(1000);
  sensor.begin(57600);
  if (!sensor.verifyPassword()) {
    lcd.clear(); lcd.print(F("Erro sensor!"));
    Serial.println(F("[ERRO] AS608"));
    while (true) { delay(1000); }
  }

  if (!rtc.begin()) {
    lcd.clear(); lcd.print(F("Erro RTC!"));
    Serial.println(F("[ERRO] RTC"));
    while (true) { delay(1000); }
  }

  Serial.println(F("[OK] Pronto."));
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(F("Sistema pronto"));
  lcd.setCursor(0, 1); lcd.print(F("Sensor OK"));
  delay(1500);
}

// =============================================
// LOOP
// =============================================
void loop() {

  // Comandos manuais via Serial Monitor
  if (Serial.available()) {
    char cmd = toupper(Serial.read());
    if (cmd == 'E') enviarChamada();
    if (cmd == 'C') {
      memset(chamada, 0, sizeof(chamada));
      totalRegistros = 0;
      Serial.println(F("[C] Limpo."));
    }
  }

  verificarEnvio();
  piscarLedVerde();

  uint8_t res = sensor.getImage();
  if (res == FINGERPRINT_NOFINGER) return;
  if (res != FINGERPRINT_OK)       return;

  digitalWrite(LED_VERDE, LOW);
  estadoLedVerde = false;
  lcd.clear(); lcd.print(F("Lendo..."));

  if (sensor.image2Tz() != FINGERPRINT_OK) { acessoNegado(); return; }
  if (sensor.fingerFastSearch() != FINGERPRINT_OK) { acessoNegado(); return; }

  Aluno* aluno = buscarAluno(sensor.fingerID);
  if (aluno != NULL) acessoPermitido(aluno);
  else               acessoNegado();
}
