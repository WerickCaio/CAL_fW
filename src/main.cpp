#include <Arduino.h>

//*************************************************************************
// FIRMWARE DE CONTROLE DE ACESSO RFID - V2.4 (Refatoração HAL - Fase 3.1)
// Mentor: Gemini
// Aluno: Werick Caio
//
// Fase 1: Camada de Hardware (GPIO e WDT)
// Fase 2: Camada de Leitura RFID (SPI e MFRC522)
// Fase 3: Camada de Banco de Dados (EEPROM e Array) com Nomes
//*************************************************************************

#include <LiquidCrystal.h>
#include <EEPROM.h>

// --- NOSSAS CAMADAS DE ABSTRAÇÃO ---
#include "hal_wdt.h"
#include "hal_gpio.h"
#include "hal_rfid.h"
#include "database.h" 

// --- CONSTANTES DE TEMPO ---
#define TEMPO_PORTA_ABERTA 1500
#define TEMPO_ACESSO_NEGADO 1000
#define TEMPO_BOOT_ADMIN 5000
#define TEMPO_ADMIN_TIMEOUT 30000
#define TEMPO_HEALTH_CHECK 3000

LiquidCrystal lcd(8, 7, 6, 4, 1, 0);

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
boolean ultimoEstadoBT = false;

// Protótipos
void setState(SystemState newState);
void pollBotao();
bool pollCartao();
void systemBeep(int quantidade);
void  migrarBancoDeDadosV2_4(); 
void setup() {
  HAL_WDT_Disable();

  Serial.begin(115200);
  Serial.println(F("\n\n========================================"));
  Serial.println(F("[BOOT] Controle de Acesso V2.4 - Iniciando"));
  Serial.println(F("========================================"));

  HAL_GPIO_Init();
  HAL_RFID_Init();
  Serial.println(F("[DEBUG] Modulos Inicializados via HAL."));

// ==========================================
  // RODE A MIGRACAO AQUI
  migrarBancoDeDadosV2_4(); 
  // ==========================================

  lcd.begin(16, 2);
  lcd.print("Iniciando...");

  Serial.println(F("[DEBUG] Lendo Banco de Dados..."));
  masterTagID = DB_LoadMaster(); 
  Serial.print(F("[BOOT] Master ID carregado: "));
  Serial.println(masterTagID != "" ? masterTagID : "NENHUM");

  delay(500);

  if (HAL_GPIO_ReadButton()) {
    Serial.println(F("[BOOT] Aguardando 5s para Modo Admin..."));
    lcd.clear();
    lcd.print("Modo Admin...");
    lcd.setCursor(0, 1);
    lcd.print("Segure por 5s");

    unsigned long bootTime = millis();
    while (HAL_GPIO_ReadButton()) {
      if (millis() - bootTime > TEMPO_BOOT_ADMIN) {
        Serial.println(F("[BOOT] Entrando no modo SET_MASTER."));
        setState(STATE_SET_MASTER);
        HAL_WDT_Enable();
        return;
      }
    }
  }

  Serial.println(F("[BOOT] Iniciando em modo normal (IDLE)."));
  setState(STATE_IDLE);
  HAL_WDT_Enable();
}

void loop() {
  HAL_WDT_Feed();

  // --- ESCUTA DA PORTA SERIAL ---
  if (Serial.available() > 0) {
    String comando = Serial.readStringUntil('\n');
    comando.trim();

    if (comando.equalsIgnoreCase("dumpCards")) {
      Serial.println(F("[COMANDO] Solicitacao de dump recebida."));
      DB_DumpToSerial();
    }
    // NOVO COMANDO: setName [TAG] [NOME]
    else if (comando.startsWith("setName ")) {
      String params = comando.substring(8); 
      int spaceIndex = params.indexOf(' '); 

      if (spaceIndex != -1) {
        String targetTag = params.substring(0, spaceIndex);
        String novoNome = params.substring(spaceIndex + 1);

        if (DB_RenameUser(targetTag, novoNome)) {
          Serial.print(F("[OK] Nome atualizado! Nova identidade: "));
          Serial.println(novoNome);
        } else {
          Serial.println(F("[ERRO] Tag nao encontrada na EEPROM."));
        }
      } else {
        Serial.println(F("[ERRO] Sintaxe incorreta. Use: setName TAG NOME"));
      }
    }
  }

  // --- HEALTH CHECK DO RFID ---
  if (millis() - lastHealthCheck > TEMPO_HEALTH_CHECK) {
    lastHealthCheck = millis();
    HAL_RFID_HealthCheck();
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
      }
      else {
        // A MAGICA ACONTECE AQUI: Pergunta ao banco de dados quem é a tag
        String nomeUsuario = DB_IdentifyUser(tagLidaAgora);

        if (nomeUsuario != "") {
          Serial.print(F("[FSM:IDLE] Acesso Liberado: "));
          Serial.println(nomeUsuario);
          setState(STATE_DOOR_OPEN);
        }
        else {
          Serial.println(F("[FSM:IDLE] Tag = DESCONHECIDA."));
          setState(STATE_ACCESS_DENIED);
        }
      }
    }
    break;

  case STATE_DOOR_OPEN:
    if (millis() - stateTimer > TEMPO_PORTA_ABERTA) {
      HAL_GPIO_RelayClose();
      setState(STATE_IDLE);
    }
    break;

  case STATE_ACCESS_DENIED:
    if (millis() - stateTimer > TEMPO_ACESSO_NEGADO) {
      HAL_GPIO_LedDeniedOff();
      setState(STATE_IDLE);
    }
    break;

  case STATE_SET_MASTER:
    if (pollCartao()) {
      masterTagID = tagLidaAgora;
      DB_SaveMaster(masterTagID); 

      lcd.clear();
      lcd.print("Master Salvo!");
      lcd.setCursor(0, 1);
      lcd.print(masterTagID.substring(0, 16));
      systemBeep(5);

      HAL_GPIO_SafeDelay(3000);
      setState(STATE_IDLE);
    }
    break;

  case STATE_ADMIN_MENU:
    if (millis() - stateTimer > TEMPO_ADMIN_TIMEOUT) {
      Serial.println(F("[FSM:ADMIN] Timeout atingido."));
      setState(STATE_IDLE);
      break;
    }

    if (pollCartao()) {
      if (tagLidaAgora.equalsIgnoreCase(masterTagID)) {
        Serial.println(F("[FSM:ADMIN] Mestre apresentado novamente. Saindo..."));
        systemBeep(1);
        setState(STATE_IDLE);
      }
      else {
        String nomeUsuario = DB_IdentifyUser(tagLidaAgora);

        if (nomeUsuario != "") {
          // A tag já existe! Vamos tentar remover da EEPROM.
          Serial.print(F("[FSM:ADMIN] Tag identificada: "));
          Serial.println(nomeUsuario);

          if (DB_RemoveUser(tagLidaAgora)) {
            Serial.println(F("[FSM:ADMIN] Acao: REMOVER tag da EEPROM."));
            lcd.clear();
            lcd.print("Tag Removida!");
            systemBeep(1);
            HAL_GPIO_SafeDelay(200);
            systemBeep(1);
          } else {
            // Se não conseguiu remover da EEPROM, mas ela existe, é LEGADA!
            Serial.println(F("[FSM:ADMIN] Erro: Tentativa de alterar tag legada."));
            lcd.clear();
            lcd.print("Tag Protegida!");
            systemBeep(2);
          }
        }
        else {
          // A tag não existe em nenhum lugar. Vamos ADICIONAR.
          Serial.println(F("[FSM:ADMIN] Acao: ADICIONAR nova tag."));
          if (DB_AddUser(tagLidaAgora)) { 
            lcd.clear();
            lcd.print("Tag Adicionada!");
            systemBeep(3);
          } else {
            lcd.clear();
            lcd.print("Memoria Cheia!");
            systemBeep(5);
          }
        }
        HAL_GPIO_SafeDelay(2000);
        setState(STATE_ADMIN_MENU);
      }
    }
    break;
  }
}

// --- FUNÇÕES AUXILIARES DA FSM ---
void setState(SystemState newState) {
  currentState = newState;
  switch (newState) {
  case STATE_IDLE:
    lcd.clear();
    lcd.print("Aproxime a Tag");
    delay(50);
    HAL_RFID_WakeUp();
    break;
  case STATE_DOOR_OPEN:
    lcd.clear();
    lcd.print("Acesso Liberado");
    HAL_GPIO_RelayOpen();
    stateTimer = millis();
    break;
  case STATE_ACCESS_DENIED:
    lcd.clear();
    lcd.print("Acesso Negado");
    HAL_GPIO_LedDeniedOn();
    stateTimer = millis();
    break;
  case STATE_SET_MASTER:
    lcd.clear();
    lcd.print("Aproxime o NOVO");
    lcd.setCursor(0, 1);
    lcd.print("Cartao Mestre");
    systemBeep(3);
    break;
  case STATE_ADMIN_MENU:
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
  boolean estadoAtualBT = HAL_GPIO_ReadButton();
  if (estadoAtualBT == true && ultimoEstadoBT == false) {
    delay(20);
    if (HAL_GPIO_ReadButton() == true) {
      setState(STATE_DOOR_OPEN);
    }
  }
  ultimoEstadoBT = estadoAtualBT;
}

bool pollCartao() {
  if (HAL_RFID_ReadCard(tagLidaAgora)) {
    return true;
  }
  return false;
}

void systemBeep(int quantidade) {
  HAL_GPIO_Beep(quantidade);
  delay(50);
  HAL_RFID_WakeUp();
}

void migrarBancoDeDadosV2_4() {
  Serial.println(F("\n========================================="));
  Serial.println(F("   [ATENCAO] INICIANDO MIGRACAO DE DADOS "));
  Serial.println(F("========================================="));
  
  // 1. Apaga fisicamente todos os 1024 bytes da EEPROM
  Serial.println(F("[1/4] Formatando a EEPROM..."));
  for (int i = 0; i < 1024; i++) {
    EEPROM.write(i, 0xFF);
  }
  
  // 2. Restaura a Tag Mestre
  Serial.println(F("[2/4] Restaurando Cartao Mestre..."));
  DB_SaveMaster("cb87aa15");

  // 3. Lista exata do seu Dump da V2.3
  String tagsParaRestaurar[8] = {
    "b95bd2b9", 
    "04134f22257980", 
    "4134f22257980", 
    "045f2c0a3a7980", 
    "04444902257980", 
    "0426515a387980", 
    "043b3b5a387980", 
    "0439425a387980"
  };

  // 4. Cadastra todo mundo no novo formato de 32 bytes
  Serial.println(F("[3/4] Restaurando os 8 usuarios antigos..."));
  for (int i = 0; i < 8; i++) {
    if (DB_AddUser(tagsParaRestaurar[i])) {
      Serial.print(F(" -> Tag "));
      Serial.print(tagsParaRestaurar[i]);
      Serial.println(F(" migrada com sucesso."));
    }
  }

  Serial.println(F("[4/4] Migracao concluida! Banco alinhado para a V2.4."));
  Serial.println(F("=========================================\n"));
}