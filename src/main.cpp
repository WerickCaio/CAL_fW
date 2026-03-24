#include <Arduino.h>

//*************************************************************************
// FIRMWARE DE CONTROLE DE ACESSO RFID - V2.0 (COMPLETO E COM TELEMETRIA)
// Mentor: Gemini
// Aluno: Werick Caio
//
// Esta versão implementa uma Máquina de Estados (FSM) robusta,
// banco de dados na EEPROM, WDT 8s, e telemetria profunda (115200 bps)
// para diagnóstico de falhas no barramento SPI/MFRC522.
//*************************************************************************

// --- 1. BIBLIOTECAS ---
#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>
#include <string.h>  // Para memset
#include <avr/wdt.h> // Watchdog Timer

// --- 2. PINOS E CONFIGURAÇÕES ---
#define BOTAO_ABRIR 5
#define RELE 3
#define SS_PIN 10
#define RST_PIN 9
#define LED_NEGADO 2

// --- CONSTANTES DO BANCO DE DADOS EEPROM ---
#define TAG_ID_LENGTH 16    // Tamanho fixo de cada slot (15 chars + 1 nulo)
#define MASTER_SLOT_ADDR 0  // Endereço (0) do slot do Mestre
#define USER_SLOTS_START 1  // O slot 1 (endereço 16) é o primeiro usuário
#define MAX_EEPROM_USERS 63 // (1024 - 16) / 16 = 63 usuários

// --- CONSTANTES DE TEMPO (em milissegundos) ---
#define TEMPO_PORTA_ABERTA 1500
#define TEMPO_ACESSO_NEGADO 1000
#define TEMPO_BOOT_ADMIN 5000     // Segurar o botão por 5s no boot
#define TEMPO_ADMIN_TIMEOUT 30000 // 30 segundos
#define TEMPO_HEALTH_CHECK 3000   // Verifica o RFID a cada 3 segundos

// --- 3. OBJETOS GLOBAIS ---
LiquidCrystal lcd(8, 7, 6, 4, 1, 0);
MFRC522 leitorRFID(SS_PIN, RST_PIN);

// --- 4. VARIÁVEIS DE ESTADO ---
enum SystemState
{
  STATE_IDLE,          // Ocioso, esperando ação
  STATE_DOOR_OPEN,     // Porta abrindo (por tag ou botão)
  STATE_ACCESS_DENIED, // Acesso negado, mostrando aviso
  STATE_SET_MASTER,    // Modo de programação: esperando tag para ser mestre
  STATE_ADMIN_MENU     // Modo Admin: Mestre passou o cartão, esperando comando
};

SystemState currentState = STATE_IDLE;
unsigned long stateTimer = 0;
unsigned long lastHealthCheck = 0; // Timer para o Health Check do RFID
String tagLidaAgora = "";
String masterTagID = "";

boolean ultimoEstadoBT = HIGH;

// O Array Antigo (Legado)
String TagsCadastradas[] = {
    // Professores//
    "c42555d3", // Prof. Lorena
    "3312a53",  // Prof. André
    "3ae17517", // Prof. Auzuir
    "79cf30c3", // Prof. Eloy
    "7a6ca07f", // P. Auzuir
    "144c67a3", // Prof. Nelio
    "d929e5b9", // Tag Gabriel // Prof. André
    "f992c5b8", // Tag Prof. Rodrigo
    // Colaboradores//
    "16454e99", // Vânia
    // BOLSISTAS//
    "9b2d4b9",       // João Paulo
    "44f75f2da7780", // Werick 2024
    "444492257980",  // Carteirinha Gabriel 2024
    "439476a387980", // João Paulo Carteirinha
    "42ed2257980",   // Pedro Henrique
    "43b3b5a387980", // Marina
    "471675a387980", // Pedro Henrique 45f2ca3a7980
    "45f2ca3a7980",  // Carteirinha Gabriel 2025
    "426515a387980", // Livia
    "439425a387980", // Aquiles
};

// --- PROTÓTIPOS DE FUNÇÕES (Para o PlatformIO / C++) ---
void safeDelay(unsigned long ms);
void setState(SystemState newState);
void pollBotao();
bool pollCartao();
bool verificarTagNoArray(String tag);
void beep(int quantidade);
String readTagFromAddr(int addr);
void writeTagToAddr(int addr, String tag);
void eraseSlot(int addr);
void salvarMasterTag(String tag);
String carregarMasterTag();
int findTagInEEPROM(String tag);
int findEmptySlot();
bool addTagToEEPROM(String tag);
bool removeTagFromEEPROM(String tag);

// --- 5. FUNÇÃO DE SETUP ---
void setup()
{
  wdt_enable(WDTO_8S);
  wdt_reset();

  Serial.begin(115200);
  Serial.println(F("\n\n========================================"));
  Serial.println(F("[BOOT] Controle de Acesso V2 - Iniciando"));
  Serial.println(F("[BOOT] Watchdog ativado (8s)"));
  Serial.println(F("========================================"));

  wdt_reset();

  SPI.begin();
  Serial.println(F("[DEBUG] SPI Inicializada."));
  leitorRFID.PCD_Init();
  Serial.println(F("[DEBUG] Modulo RFID Inicializado."));

  wdt_reset();

  pinMode(RELE, OUTPUT);
  pinMode(LED_NEGADO, OUTPUT);
  pinMode(BOTAO_ABRIR, INPUT_PULLUP);

  digitalWrite(RELE, LOW);
  digitalWrite(LED_NEGADO, LOW);

  lcd.begin(16, 2);
  lcd.print("Iniciando...");

  Serial.println(F("[DEBUG] Lendo EEPROM..."));
  masterTagID = carregarMasterTag();
  Serial.print(F("[BOOT] Master ID carregado: "));
  Serial.println(masterTagID != "" ? masterTagID : "NENHUM");

  wdt_reset();

  delay(500);
  wdt_reset();

  if (digitalRead(BOTAO_ABRIR) == LOW)
  {
    Serial.println(F("[BOOT] Botao pressionado no boot. Aguardando 5s para Admin..."));
    lcd.clear();
    lcd.print("Modo Admin...");
    lcd.setCursor(0, 1);
    lcd.print("Segure por 5s");

    unsigned long bootTime = millis();
    while (digitalRead(BOTAO_ABRIR) == LOW)
    {
      wdt_reset();
      if (millis() - bootTime > TEMPO_BOOT_ADMIN)
      {
        Serial.println(F("[BOOT] Entrando no modo SET_MASTER."));
        setState(STATE_SET_MASTER);
        return;
      }
    }
    Serial.println(F("[BOOT] Botao solto antes dos 5s. Cancelado."));
  }

  Serial.println(F("[BOOT] Iniciando em modo normal (IDLE)."));
  setState(STATE_IDLE);
}

// --- 6. LOOP PRINCIPAL ---
void loop()
{
  wdt_reset();

  // --- HEALTH CHECK DO RFID ---
  if (millis() - lastHealthCheck > TEMPO_HEALTH_CHECK)
  {
    lastHealthCheck = millis();
    byte versao = leitorRFID.PCD_ReadRegister(leitorRFID.VersionReg);
    Serial.print(F("[TELEMETRIA] Versao MFRC522: 0x"));
    Serial.println(versao, HEX);

    if (versao == 0x00 || versao == 0xFF)
    {
      Serial.println(F("[ERRO CRITICO] Módulo RFID travou (Falha SPI)! Tentando recuperar..."));
      SPI.begin(); // As vezes o barramento SPI inteiro cai
      leitorRFID.PCD_Init();
      delay(50);
      Serial.println(F("[DEBUG] Modulo RFID reinicializado."));
    }
  }

  switch (currentState)
  {

  case STATE_IDLE:
    pollBotao();

    if (pollCartao())
    {
      Serial.print(F("[FSM:IDLE] Processando tag: "));
      Serial.println(tagLidaAgora);

      if (tagLidaAgora.equalsIgnoreCase(masterTagID) && masterTagID != "")
      {
        Serial.println(F("[FSM:IDLE] Tag = MESTRE."));
        setState(STATE_ADMIN_MENU);
      }
      else if (findTagInEEPROM(tagLidaAgora) != -1)
      {
        Serial.println(F("[FSM:IDLE] Tag = Usuario EEPROM."));
        setState(STATE_DOOR_OPEN);
      }
      else if (verificarTagNoArray(tagLidaAgora))
      {
        Serial.println(F("[FSM:IDLE] Tag = Usuario LEGADO."));
        setState(STATE_DOOR_OPEN);
      }
      else
      {
        Serial.println(F("[FSM:IDLE] Tag = DESCONHECIDA."));
        setState(STATE_ACCESS_DENIED);
      }
    }
    break;

  case STATE_DOOR_OPEN:
    if (millis() - stateTimer > TEMPO_PORTA_ABERTA)
    {
      Serial.println(F("[FSM:DOOR_OPEN] Fechando porta, voltando para IDLE."));
      digitalWrite(RELE, LOW);
      setState(STATE_IDLE);
    }
    break;

  case STATE_ACCESS_DENIED:
    if (millis() - stateTimer > TEMPO_ACESSO_NEGADO)
    {
      digitalWrite(LED_NEGADO, LOW);
      setState(STATE_IDLE);
    }
    break;

  case STATE_SET_MASTER:
    if (pollCartao())
    {
      Serial.print(F("[FSM:SET_MASTER] Nova Tag Mestre lida: "));
      Serial.println(tagLidaAgora);

      masterTagID = tagLidaAgora;
      salvarMasterTag(masterTagID);

      lcd.clear();
      lcd.print("Master Salvo!");
      lcd.setCursor(0, 1);
      lcd.print(masterTagID.substring(0, 16));
      beep(5);

      Serial.println(F("[DEBUG] Pausa de 3s apos salvar mestre..."));
      safeDelay(3000);
      setState(STATE_IDLE);
    }
    break;

  case STATE_ADMIN_MENU:
    if (millis() - stateTimer > TEMPO_ADMIN_TIMEOUT)
    {
      Serial.println(F("[FSM:ADMIN] Timeout atingido. Voltando para IDLE."));
      setState(STATE_IDLE);
      break;
    }

    if (pollCartao())
    {
      Serial.print(F("[FSM:ADMIN] Tag apresentada para gerencia: "));
      Serial.println(tagLidaAgora);

      if (tagLidaAgora.equalsIgnoreCase(masterTagID))
      {
        Serial.println(F("[FSM:ADMIN] Mestre apresentado novamente. Saindo..."));
        beep(1);
        setState(STATE_IDLE);
      }
      else if (verificarTagNoArray(tagLidaAgora))
      {
        Serial.println(F("[FSM:ADMIN] Erro: Tentativa de alterar tag legada."));
        lcd.clear();
        lcd.print("Tag Protegida!");
        beep(2);
        safeDelay(2000);
        setState(STATE_ADMIN_MENU);
      }
      else
      {
        Serial.println(F("[DEBUG] Buscando tag na EEPROM..."));
        int tagAddr = findTagInEEPROM(tagLidaAgora);

        if (tagAddr != -1)
        {
          Serial.println(F("[FSM:ADMIN] Acao: REMOVER tag existente."));
          removeTagFromEEPROM(tagLidaAgora);
          lcd.clear();
          lcd.print("Tag Removida!");
          beep(1);
          safeDelay(200);
          beep(1);
          safeDelay(2000);
          setState(STATE_ADMIN_MENU);
        }
        else
        {
          Serial.println(F("[FSM:ADMIN] Acao: ADICIONAR nova tag."));
          if (addTagToEEPROM(tagLidaAgora))
          {
            Serial.println(F("[DEBUG] Tag adicionada com sucesso."));
            lcd.clear();
            lcd.print("Tag Adicionada!");
            beep(3);
          }
          else
          {
            Serial.println(F("[DEBUG] Erro: EEPROM Cheia."));
            lcd.clear();
            lcd.print("Memoria Cheia!");
            beep(5);
          }
          safeDelay(2000);
          setState(STATE_ADMIN_MENU);
        }
      }
    }
    break;
  }
}

// --- 7. FUNÇÕES AUXILIARES ---
void safeDelay(unsigned long ms)
{
  unsigned long start = millis();
  while (millis() - start < ms)
  {
    wdt_reset();
    delay(1);
  }
}

void setState(SystemState newState)
{
  currentState = newState;
  Serial.print(F("[STATE_CHANGE] Novo estado: "));

  switch (newState)
  {
  case STATE_IDLE:
    Serial.println(F("STATE_IDLE"));
    lcd.clear();
    lcd.print("Aproxime a Tag");

    // ADICIONE ESTAS DUAS LINHAS:
    delay(50);             // Dá um tempinho para o ruído do relé dissipar
    leitorRFID.PCD_Init(); // Reinicia a antena do RFID
    break;

  case STATE_DOOR_OPEN:
    Serial.println(F("STATE_DOOR_OPEN"));
    lcd.clear();
    lcd.print("Acesso Liberado");
    digitalWrite(RELE, HIGH);
    stateTimer = millis();
    break;

  case STATE_ACCESS_DENIED:
    Serial.println(F("STATE_ACCESS_DENIED"));
    lcd.clear();
    lcd.print("Acesso Negado");
    digitalWrite(LED_NEGADO, HIGH);
    stateTimer = millis();
    break;

  case STATE_SET_MASTER:
    Serial.println(F("STATE_SET_MASTER"));
    lcd.clear();
    lcd.print("Aproxime o NOVO");
    lcd.setCursor(0, 1);
    lcd.print("Cartao Mestre");
    beep(3);
    break;

  case STATE_ADMIN_MENU:
    Serial.println(F("STATE_ADMIN_MENU"));
    lcd.clear();
    lcd.print("Modo Admin");
    lcd.setCursor(0, 1);
    lcd.print("Aproxime a Tag");
    beep(2);
    stateTimer = millis();
    break;
  }
}

void pollBotao()
{
  boolean estadoAtualBT = digitalRead(BOTAO_ABRIR);
  if (estadoAtualBT == LOW && ultimoEstadoBT == HIGH)
  {
    delay(20);
    if (digitalRead(BOTAO_ABRIR) == LOW)
    {
      Serial.println(F("[EVENTO] Botao de saida pressionado."));
      setState(STATE_DOOR_OPEN);
    }
  }
  ultimoEstadoBT = estadoAtualBT;
}

bool pollCartao()
{
  if (!leitorRFID.PICC_IsNewCardPresent() || !leitorRFID.PICC_ReadCardSerial())
  {
    return false;
  }

  tagLidaAgora = "";
  for (byte i = 0; i < leitorRFID.uid.size; i++)
  {
    if (leitorRFID.uid.uidByte[i] < 0x10)
    {
      tagLidaAgora += "0";
    }
    tagLidaAgora += String(leitorRFID.uid.uidByte[i], HEX);
  }

  leitorRFID.PICC_HaltA();
  // leitorRFID.PCD_StopCrypto1(); // Importante para liberar o SPI apos a leitura

  Serial.print(F("[EVENTO RFID] Cartao lido fisicamente: "));
  Serial.println(tagLidaAgora);
  return true;
}

bool verificarTagNoArray(String tag)
{
  int totalTags = sizeof(TagsCadastradas) / sizeof(String);
  for (int i = 0; i < totalTags; i++)
  {
    if (tag.equalsIgnoreCase(TagsCadastradas[i]))
    {
      return true;
    }
  }
  return false;
}

void beep(int quantidade)
{
  for (int i = 0; i < quantidade; i++)
  {
    digitalWrite(RELE, HIGH); // Assumindo que o Buzzer esta no mesmo pino do rele
    safeDelay(200);
    digitalWrite(RELE, LOW);
    if (i < quantidade - 1)
    {
      safeDelay(100);
    }
  }

  // --- O REMÉDIO CONTRA O RUÍDO DA BOBINA ---
  delay(50);             // Dá 50ms para a energia reversa (flyback) do relé dissipar
  leitorRFID.PCD_Init(); // Acorda a antena do módulo imediatamente!
}

// --- Funções da EEPROM ---
String readTagFromAddr(int addr)
{
  char tagChars[TAG_ID_LENGTH];
  EEPROM.get(addr, tagChars);

  if (tagChars[0] == (char)0xFF || tagChars[0] == 0x00)
  {
    return "";
  }
  tagChars[TAG_ID_LENGTH - 1] = '\0';
  return String(tagChars);
}

void writeTagToAddr(int addr, String tag)
{
  char tagChars[TAG_ID_LENGTH];
  memset(tagChars, 0, TAG_ID_LENGTH);
  tag.toCharArray(tagChars, TAG_ID_LENGTH);
  EEPROM.put(addr, tagChars);
  Serial.print(F("[EEPROM] Tag salva no endereco: "));
  Serial.println(addr);
}

void eraseSlot(int addr)
{
  char emptySlot[TAG_ID_LENGTH];
  memset(emptySlot, 0xFF, TAG_ID_LENGTH);
  EEPROM.put(addr, emptySlot);
  Serial.print(F("[EEPROM] Slot apagado no endereco: "));
  Serial.println(addr);
}

void salvarMasterTag(String tag)
{
  Serial.println(F("[EEPROM] Salvando Master..."));
  writeTagToAddr(MASTER_SLOT_ADDR, tag);
}

String carregarMasterTag()
{
  return readTagFromAddr(MASTER_SLOT_ADDR);
}

int findTagInEEPROM(String tag)
{
  for (int i = 0; i < MAX_EEPROM_USERS; i++)
  {
    int addr = (USER_SLOTS_START + i) * TAG_ID_LENGTH;
    String tagInSlot = readTagFromAddr(addr);
    if (tag.equalsIgnoreCase(tagInSlot))
    {
      return addr;
    }
  }
  return -1;
}

int findEmptySlot()
{
  for (int i = 0; i < MAX_EEPROM_USERS; i++)
  {
    int addr = (USER_SLOTS_START + i) * TAG_ID_LENGTH;
    if (EEPROM.read(addr) == 0xFF || EEPROM.read(addr) == 0x00)
    {
      return addr;
    }
  }
  return -1;
}

bool addTagToEEPROM(String tag)
{
  int emptyAddr = findEmptySlot();
  if (emptyAddr == -1)
  {
    return false;
  }
  writeTagToAddr(emptyAddr, tag);
  return true;
}

bool removeTagFromEEPROM(String tag)
{
  int tagAddr = findTagInEEPROM(tag);
  if (tagAddr == -1)
  {
    return false;
  }
  eraseSlot(tagAddr);
  return true;
}