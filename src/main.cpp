#include <Arduino.h>

//*************************************************************************
// FIRMWARE DE CONTROLE DE ACESSO RFID - V2.1 (Refatoração HAL - Fase 1)
// Mentor: Gemini
// Aluno: Werick Caio
//
// Fase 1: Camada de Hardware (GPIO e WDT) isolada com sucesso.
//*************************************************************************

// --- 1. BIBLIOTECAS ---
#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>
#include <string.h>  

// --- NOSSAS CAMADAS DE ABSTRAÇÃO (HAL) ---
#include "hal_wdt.h"
#include "hal_gpio.h"

// --- 2. PINOS E CONFIGURAÇÕES (Restantes) ---
// Note que os pinos de Relé, Botão e LED sumiram daqui! Estão na HAL.
#define SS_PIN 10
#define RST_PIN 9

// --- CONSTANTES DO BANCO DE DADOS EEPROM ---
#define TAG_ID_LENGTH 16    
#define MASTER_SLOT_ADDR 0  
#define USER_SLOTS_START 1  
#define MAX_EEPROM_USERS 63 

// --- CONSTANTES DE TEMPO ---
#define TEMPO_PORTA_ABERTA 1500
#define TEMPO_ACESSO_NEGADO 1000
#define TEMPO_BOOT_ADMIN 5000     
#define TEMPO_ADMIN_TIMEOUT 30000 
#define TEMPO_HEALTH_CHECK 3000   

// --- 3. OBJETOS GLOBAIS ---
LiquidCrystal lcd(8, 7, 6, 4, 1, 0);
MFRC522 leitorRFID(SS_PIN, RST_PIN);

// --- 4. VARIÁVEIS DE ESTADO ---
enum SystemState {
  STATE_IDLE,          
  STATE_DOOR_OPEN,     
  STATE_ACCESS_DENIED, 
  STATE_SET_MASTER,    
  STATE_ADMIN_MENU     
};

SystemState currentState = STATE_IDLE;
unsigned long stateTimer = 0;
unsigned long lastHealthCheck = 0; 
String tagLidaAgora = "";
String masterTagID = "";

// Adaptado para a HAL: false = não pressionado, true = pressionado
boolean ultimoEstadoBT = false;
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

// --- PROTÓTIPOS DE FUNÇÕES LOCAIS ---
void setState(SystemState newState);
void pollBotao();
bool pollCartao();
bool verificarTagNoArray(String tag);
void systemBeep(int quantidade); // Wrapper para juntar o Beep com o Reset do RFID
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
void setup() {
  // 1. A PRIMEIRA COISA: Desliga o WDT para evitar o Bootloop!
  HAL_WDT_Disable();

  Serial.begin(115200);
  Serial.println(F("\n\n========================================"));
  Serial.println(F("[BOOT] Controle de Acesso V2.1 - Iniciando"));
  Serial.println(F("========================================"));

  // 2. Inicializa os Pinos pela HAL (Relé, LED e Botão)
  HAL_GPIO_Init();

  SPI.begin();
  Serial.println(F("[DEBUG] SPI Inicializada."));
  leitorRFID.PCD_Init();
  Serial.println(F("[DEBUG] Modulo RFID Inicializado."));

  lcd.begin(16, 2);
  lcd.print("Iniciando...");

  Serial.println(F("[DEBUG] Lendo EEPROM..."));
  masterTagID = carregarMasterTag();
  Serial.print(F("[BOOT] Master ID carregado: "));
  Serial.println(masterTagID != "" ? masterTagID : "NENHUM");

  delay(500);

  // 3. Lógica do botão Admin no boot usando a HAL
  if (HAL_GPIO_ReadButton()) {
    Serial.println(F("[BOOT] Botao pressionado no boot. Aguardando 5s para Admin..."));
    lcd.clear();
    lcd.print("Modo Admin...");
    lcd.setCursor(0, 1);
    lcd.print("Segure por 5s");

    unsigned long bootTime = millis();
    while (HAL_GPIO_ReadButton()) {
      if (millis() - bootTime > TEMPO_BOOT_ADMIN) {
        Serial.println(F("[BOOT] Entrando no modo SET_MASTER."));
        setState(STATE_SET_MASTER);
        HAL_WDT_Enable(); // Liga o cão de guarda antes de sair
        return;
      }
    }
    Serial.println(F("[BOOT] Botao solto antes dos 5s. Cancelado."));
  }

  Serial.println(F("[BOOT] Iniciando em modo normal (IDLE)."));
  setState(STATE_IDLE);
  
  // 4. A ÚLTIMA COISA: Liga o Watchdog para proteger o loop principal!
  HAL_WDT_Enable(); 
}

// --- 6. LOOP PRINCIPAL ---
void loop() {
  // Alimentando o cão de guarda pela HAL
  HAL_WDT_Feed();

  // --- HEALTH CHECK DO RFID ---
  if (millis() - lastHealthCheck > TEMPO_HEALTH_CHECK) {
    lastHealthCheck = millis();
    byte versao = leitorRFID.PCD_ReadRegister(leitorRFID.VersionReg);
    
    if (versao == 0x00 || versao == 0xFF) {
      Serial.println(F("[ERRO CRITICO] Módulo RFID travou (Falha SPI)! Tentando recuperar..."));
      SPI.begin(); 
      leitorRFID.PCD_Init();
      delay(50);
      Serial.println(F("[DEBUG] Modulo RFID reinicializado."));
    }
  }

  switch (currentState) {

  case STATE_IDLE:
    pollBotao();

    if (pollCartao()) {
      Serial.print(F("[FSM:IDLE] Processando tag: "));
      Serial.println(tagLidaAgora);

      if (tagLidaAgora.equalsIgnoreCase(masterTagID) && masterTagID != "") {
        Serial.println(F("[FSM:IDLE] Tag = MESTRE."));
        setState(STATE_ADMIN_MENU);
      } else if (findTagInEEPROM(tagLidaAgora) != -1) {
        Serial.println(F("[FSM:IDLE] Tag = Usuario EEPROM."));
        setState(STATE_DOOR_OPEN);
      } else if (verificarTagNoArray(tagLidaAgora)) {
        Serial.println(F("[FSM:IDLE] Tag = Usuario LEGADO."));
        setState(STATE_DOOR_OPEN);
      } else {
        Serial.println(F("[FSM:IDLE] Tag = DESCONHECIDA."));
        setState(STATE_ACCESS_DENIED);
      }
    }
    break;

  case STATE_DOOR_OPEN:
    if (millis() - stateTimer > TEMPO_PORTA_ABERTA) {
      Serial.println(F("[FSM:DOOR_OPEN] Fechando porta, voltando para IDLE."));
      HAL_GPIO_RelayClose(); // Usando a HAL para fechar a porta
      setState(STATE_IDLE);
    }
    break;

  case STATE_ACCESS_DENIED:
    if (millis() - stateTimer > TEMPO_ACESSO_NEGADO) {
      HAL_GPIO_LedDeniedOff(); // Usando a HAL para apagar o LED
      setState(STATE_IDLE);
    }
    break;

  case STATE_SET_MASTER:
    if (pollCartao()) {
      Serial.print(F("[FSM:SET_MASTER] Nova Tag Mestre lida: "));
      Serial.println(tagLidaAgora);

      masterTagID = tagLidaAgora;
      salvarMasterTag(masterTagID);

      lcd.clear();
      lcd.print("Master Salvo!");
      lcd.setCursor(0, 1);
      lcd.print(masterTagID.substring(0, 16));
      systemBeep(5);

      Serial.println(F("[DEBUG] Pausa de 3s apos salvar mestre..."));
      HAL_GPIO_SafeDelay(3000); // Usando a HAL para o delay seguro
      setState(STATE_IDLE);
    }
    break;

  case STATE_ADMIN_MENU:
    if (millis() - stateTimer > TEMPO_ADMIN_TIMEOUT) {
      Serial.println(F("[FSM:ADMIN] Timeout atingido. Voltando para IDLE."));
      setState(STATE_IDLE);
      break;
    }

    if (pollCartao()) {
      if (tagLidaAgora.equalsIgnoreCase(masterTagID)) {
        Serial.println(F("[FSM:ADMIN] Mestre apresentado novamente. Saindo..."));
        systemBeep(1);
        setState(STATE_IDLE);
      } else if (verificarTagNoArray(tagLidaAgora)) {
        Serial.println(F("[FSM:ADMIN] Erro: Tentativa de alterar tag legada."));
        lcd.clear();
        lcd.print("Tag Protegida!");
        systemBeep(2);
        HAL_GPIO_SafeDelay(2000);
        setState(STATE_ADMIN_MENU);
      } else {
        int tagAddr = findTagInEEPROM(tagLidaAgora);

        if (tagAddr != -1) {
          Serial.println(F("[FSM:ADMIN] Acao: REMOVER tag existente."));
          removeTagFromEEPROM(tagLidaAgora);
          lcd.clear();
          lcd.print("Tag Removida!");
          systemBeep(1);
          HAL_GPIO_SafeDelay(200);
          systemBeep(1);
          HAL_GPIO_SafeDelay(2000);
          setState(STATE_ADMIN_MENU);
        } else {
          Serial.println(F("[FSM:ADMIN] Acao: ADICIONAR nova tag."));
          if (addTagToEEPROM(tagLidaAgora)) {
            lcd.clear();
            lcd.print("Tag Adicionada!");
            systemBeep(3);
          } else {
            lcd.clear();
            lcd.print("Memoria Cheia!");
            systemBeep(5);
          }
          HAL_GPIO_SafeDelay(2000);
          setState(STATE_ADMIN_MENU);
        }
      }
    }
    break;
  }
}

// --- 7. FUNÇÕES AUXILIARES DA FSM ---

void setState(SystemState newState) {
  currentState = newState;
  Serial.print(F("[STATE_CHANGE] Novo estado: "));

  switch (newState) {
  case STATE_IDLE:
    Serial.println(F("STATE_IDLE"));
    lcd.clear();
    lcd.print("Aproxime a Tag");
    delay(50);             
    leitorRFID.PCD_Init(); 
    break;

  case STATE_DOOR_OPEN:
    Serial.println(F("STATE_DOOR_OPEN"));
    lcd.clear();
    lcd.print("Acesso Liberado");
    HAL_GPIO_RelayOpen(); // Ação via HAL
    stateTimer = millis();
    break;

  case STATE_ACCESS_DENIED:
    Serial.println(F("STATE_ACCESS_DENIED"));
    lcd.clear();
    lcd.print("Acesso Negado");
    HAL_GPIO_LedDeniedOn(); // Ação via HAL
    stateTimer = millis();
    break;

  case STATE_SET_MASTER:
    Serial.println(F("STATE_SET_MASTER"));
    lcd.clear();
    lcd.print("Aproxime o NOVO");
    lcd.setCursor(0, 1);
    lcd.print("Cartao Mestre");
    systemBeep(3);
    break;

  case STATE_ADMIN_MENU:
    Serial.println(F("STATE_ADMIN_MENU"));
    lcd.clear();
    lcd.print("Modo Admin");
    lcd.setCursor(0, 1);
    lcd.print("Aproxime a Tag");
    systemBeep(2);
    stateTimer = millis();
    break;
  }
}

void pollBotao() {
  boolean estadoAtualBT = HAL_GPIO_ReadButton(); // Leitura via HAL
  if (estadoAtualBT == true && ultimoEstadoBT == false) {
    delay(20); // Debounce
    if (HAL_GPIO_ReadButton() == true) {
      Serial.println(F("[EVENTO] Botao de saida pressionado."));
      setState(STATE_DOOR_OPEN);
    }
  }
  ultimoEstadoBT = estadoAtualBT;
}

bool pollCartao() {
  if (!leitorRFID.PICC_IsNewCardPresent() || !leitorRFID.PICC_ReadCardSerial()) {
    return false;
  }

  tagLidaAgora = "";
  for (byte i = 0; i < leitorRFID.uid.size; i++) {
    if (leitorRFID.uid.uidByte[i] < 0x10) {
      tagLidaAgora += "0";
    }
    tagLidaAgora += String(leitorRFID.uid.uidByte[i], HEX);
  }

  leitorRFID.PICC_HaltA();
  return true;
}

bool verificarTagNoArray(String tag) {
  int totalTags = sizeof(TagsCadastradas) / sizeof(String);
  for (int i = 0; i < totalTags; i++) {
    if (tag.equalsIgnoreCase(TagsCadastradas[i])) {
      return true;
    }
  }
  return false;
}

// Criamos esse wrapper no main para não misturar lógica de RFID dentro do hal_gpio
void systemBeep(int quantidade) {
  HAL_GPIO_Beep(quantidade); // Chama a rotina de hardware
  delay(50);             
  leitorRFID.PCD_Init();     // Reinicia a antena após o ruído do relé
}

// =========================================================================
// AS FUNÇÕES DA EEPROM CONTINUAM AQUI (Serão isoladas na Fase 2)
// =========================================================================

String readTagFromAddr(int addr) {
  char tagChars[TAG_ID_LENGTH];
  EEPROM.get(addr, tagChars);
  if (tagChars[0] == (char)0xFF || tagChars[0] == 0x00) return "";
  tagChars[TAG_ID_LENGTH - 1] = '\0';
  return String(tagChars);
}

void writeTagToAddr(int addr, String tag) {
  char tagChars[TAG_ID_LENGTH];
  memset(tagChars, 0, TAG_ID_LENGTH);
  tag.toCharArray(tagChars, TAG_ID_LENGTH);
  EEPROM.put(addr, tagChars);
}

void eraseSlot(int addr) {
  char emptySlot[TAG_ID_LENGTH];
  memset(emptySlot, 0xFF, TAG_ID_LENGTH);
  EEPROM.put(addr, emptySlot);
}

void salvarMasterTag(String tag) { writeTagToAddr(MASTER_SLOT_ADDR, tag); }
String carregarMasterTag() { return readTagFromAddr(MASTER_SLOT_ADDR); }

int findTagInEEPROM(String tag) {
  for (int i = 0; i < MAX_EEPROM_USERS; i++) {
    int addr = (USER_SLOTS_START + i) * TAG_ID_LENGTH;
    if (tag.equalsIgnoreCase(readTagFromAddr(addr))) return addr;
  }
  return -1;
}

int findEmptySlot() {
  for (int i = 0; i < MAX_EEPROM_USERS; i++) {
    int addr = (USER_SLOTS_START + i) * TAG_ID_LENGTH;
    if (EEPROM.read(addr) == 0xFF || EEPROM.read(addr) == 0x00) return addr;
  }
  return -1;
}

bool addTagToEEPROM(String tag) {
  int emptyAddr = findEmptySlot();
  if (emptyAddr == -1) return false;
  writeTagToAddr(emptyAddr, tag);
  return true;
}

bool removeTagFromEEPROM(String tag) {
  int tagAddr = findTagInEEPROM(tag);
  if (tagAddr == -1) return false;
  eraseSlot(tagAddr);
  return true;
}