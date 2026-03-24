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

// =========================================================================
// SISTEMA DE LOGS NA RAM (BUFFER CIRCULAR) - MVP
// =========================================================================
#define MAX_LOGS 20 // Guardar os últimos 20 acessos na RAM

struct AccessLog
{
  unsigned long timestamp; // Tempo em milissegundos
  char name[16];           // Nome do utilizador
};

AccessLog logBuffer[MAX_LOGS];
int logHead = 0;  // Aponta para a próxima posição livre
int logCount = 0; // Conta quantos logs já temos

// Função para adicionar um novo registo
void addLog(String userName)
{
  logBuffer[logHead].timestamp = millis();

  // Limpa o espaço e copia o nome com segurança (máx 15 caracteres)
  memset(logBuffer[logHead].name, 0, 16);
  strncpy(logBuffer[logHead].name, userName.c_str(), 15);

  // A Mágica do Buffer Circular: se chegar ao fim (20), volta para o 0
  logHead = (logHead + 1) % MAX_LOGS;
  if (logCount < MAX_LOGS)
  {
    logCount++;
  }
}

// Função para imprimir os registos no Monitor Serial
void printLogs()
{
  Serial.println(F("\n========================================="));
  Serial.println(F("       ULTIMOS ACESSOS (RAM LOGS)        "));
  Serial.println(F("========================================="));

  if (logCount == 0)
  {
    Serial.println(F("Nenhum acesso registado ainda desde o boot."));
  }
  else
  {
    // Calcula por onde começar a ler para imprimir por ordem cronológica
    int startIdx = (logCount == MAX_LOGS) ? logHead : 0;

    for (int i = 0; i < logCount; i++)
    {
      int idx = (startIdx + i) % MAX_LOGS;

      Serial.print(F("[Tempo: "));
      Serial.print(logBuffer[idx].timestamp);
      Serial.print(F(" ms] - Acesso: "));
      Serial.println(logBuffer[idx].name);
    }
  }
  Serial.println(F("=========================================\n"));
}
// =========================================================================

enum SystemState
{
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

void restaurarBancoDeDados()
{
  Serial.println(F("\n[SISTEMA] Formatando a EEPROM 100% limpa (Aguarde)..."));
  for (int i = 0; i < 1024; i++)
  {
    EEPROM.update(i, 0xFF);
  }
  Serial.println(F("[SISTEMA] EEPROM Formatada com 0xFF."));

  Serial.println(F("\n[SISTEMA] Gravando TAG MESTRE..."));
  DB_SaveMaster("cb87aa15");

  const char *backupTags[] = {
      "b95bd2b9",
      "04134f22257980",
      "4134f22257980",
      "045f2c0a3a7980",
      "04444902257980",
      "0426515a387980",
      "043b3b5a387980",
      "0439425a387980"};

  Serial.println(F("\n[SISTEMA] Iniciando injecao das 8 tags..."));
  for (int i = 0; i < 8; i++)
  {
    Serial.print(F("\n--- Adicionando Tag "));
    Serial.print(i + 1);
    Serial.println(F(" ---"));
    DB_AddUser(String(backupTags[i]));
  }

  Serial.println(F("\n================================================="));
  Serial.println(F("[SISTEMA] RESTAURACAO CONCLUIDA!"));
  Serial.println(F("================================================="));
}

void dumpEEPROMRawHex()
{
  Serial.println(F("\n======================================================================================================="));
  Serial.println(F("                                RAW EEPROM HEX DUMP (Slots de 32 Bytes)"));
  Serial.println(F("======================================================================================================="));

  // Lê a EEPROM inteira pulando de 32 em 32 bytes (Exatamente o tamanho do nosso Slot!)
  for (int addr = 0; addr < 1024; addr += 32)
  {

    // 1. Imprime o endereço inicial daquele bloco (ex: [0000], [0032], [0064])
    char addrStr[10];
    sprintf(addrStr, "[%04d] ", addr);
    Serial.print(addrStr);

    // 2. Imprime os 32 bytes em formato Hexadecimal (Ex: 63 62 38 37...)
    for (int i = 0; i < 32; i++)
    {
      byte b = EEPROM.read(addr + i);
      if (b < 0x10)
        Serial.print("0"); // Adiciona o zero à esquerda para ficar alinhado
      Serial.print(b, HEX);
      Serial.print(" ");

      // Coloca um separador visual entre a TAG (16 bytes) e o NOME (16 bytes)
      if (i == 15)
        Serial.print("- ");
    }

    Serial.print(" | ");

    // 3. Imprime os mesmos 32 bytes convertidos para Texto (ASCII)
    for (int i = 0; i < 32; i++)
    {
      byte b = EEPROM.read(addr + i);
      // Se for um caractere de texto legível (letras, números, espaços)
      if (b >= 32 && b <= 126)
      {
        Serial.print((char)b);
      }
      else
      {
        Serial.print("."); // Se for lixo de memória ou nulo (0x00 / 0xFF), imprime um ponto
      }
    }
    Serial.println(); // Pula para a linha do próximo slot
  }
  Serial.println(F("=======================================================================================================\n"));
}

void imprimirMenuAjuda()
{
  Serial.println(F("\n==================================================="));
  Serial.println(F("       TERMINAL DE COMANDOS - ACESSO RFID          "));
  Serial.println(F("==================================================="));
  Serial.println(F(" Comandos disponiveis:"));
  Serial.println(F("   help        - Mostra este menu de ajuda"));
  Serial.println(F("   dumpCards   - Lista todos os usuarios e vagas"));
  Serial.println(F("   rawDump     - Raio-X Hexadecimal da EEPROM"));
  Serial.println(F("   showLogs    - Mostra os ultimos 20 acessos (RAM)"));
  Serial.println(F("   setMaster   - Entra no modo de gravar Cartao Mestre"));
  Serial.println(F("   setName X Y - Ex: setName 04134f22257980 Livia"));
  Serial.println(F("===================================================\n"));
}

void setup()
{
  HAL_WDT_Disable(); // Desliga o cão de guarda imediatamente

  Serial.begin(115200);
  Serial.println(F("\n\n========================================"));
  Serial.println(F("[BOOT] Controle de Acesso V2.4 - Iniciando"));
  Serial.println(F("========================================"));

  // =================================================================
  // CHAME A MIGRACAO AQUI (ANTES DE INICIAR SPI, PINOS E LCD)
  // migracaoCirurgicaEEPROM();
  // restaurarBancoDeDados();

  // AQUI: Chama o Raio-X da Memória!
  // dumpEEPROMRawHex();
  // =================================================================

  HAL_GPIO_Init();
  HAL_RFID_Init();
  Serial.println(F("[DEBUG] Modulos Inicializados via HAL."));

  lcd.begin(16, 2);
  lcd.print("Iniciando...");

  Serial.println(F("[DEBUG] Lendo Banco de Dados..."));
  masterTagID = DB_LoadMaster();
  Serial.print(F("[BOOT] Master ID carregado: "));
  Serial.println(masterTagID != "" ? masterTagID : "NENHUM");

  delay(500);
  if (HAL_GPIO_ReadButton())
  {
    Serial.println(F("[BOOT] Aguardando 5s para Modo Admin..."));
    lcd.clear();
    lcd.print("Modo Admin...");
    lcd.setCursor(0, 1);
    lcd.print("Segure por 5s");

    unsigned long bootTime = millis();
    while (HAL_GPIO_ReadButton())
    {
      if (millis() - bootTime > TEMPO_BOOT_ADMIN)
      {
        Serial.println(F("[BOOT] Entrando no modo SET_MASTER."));
        setState(STATE_SET_MASTER);
        HAL_WDT_Enable();
        return;
      }
    }
  }

  Serial.println(F("[BOOT] Iniciando em modo normal (IDLE)."));

  // Imprime o menu para o usuario saber o que pode digitar
  imprimirMenuAjuda();

  setState(STATE_IDLE);
  HAL_WDT_Enable();
}

void loop()
{
  HAL_WDT_Feed();

  // --- ESCUTA DA PORTA SERIAL ---

  // --- ESCUTA DA PORTA SERIAL ---
  if (Serial.available() > 0)
  {
    String comando = Serial.readStringUntil('\n');
    comando.trim();

    // 1. Comando de Ajuda
    if (comando.equalsIgnoreCase("help"))
    {
      imprimirMenuAjuda();
    }
    // 2. Dump de Usuários formatado
    else if (comando.equalsIgnoreCase("dumpCards"))
    {
      Serial.println(F("[COMANDO] Solicitacao de dump de usuarios recebida."));
      DB_DumpToSerial();
    }
    // 3. Raio-X Hexadecimal da Memória
    else if (comando.equalsIgnoreCase("rawDump"))
    {
      Serial.println(F("[COMANDO] Solicitacao de RAW DUMP Hexadecimal recebida."));
      dumpEEPROMRawHex(); // A função que criamos na etapa anterior!
    }
    // 4. Exibir Logs da RAM
    else if (comando.equalsIgnoreCase("showLogs"))
    {
      Serial.println(F("[COMANDO] Solicitacao de logs recebida."));
      printLogs();
    }
    // 5. Acionar a gravação do Master via Serial
    else if (comando.equalsIgnoreCase("setMaster"))
    {
      Serial.println(F("[COMANDO] Solicitacao para gravar NOVO MESTRE."));
      Serial.println(F("[AVISO] Aproxime a nova Tag Mestre do leitor..."));
      // A MÁGICA DA FSM: Apenas mudamos o estado e o loop faz o resto!
      setState(STATE_SET_MASTER);
    }
    // 6. Configurar Nomes
    else if (comando.startsWith("setName "))
    {
      String params = comando.substring(8);
      int spaceIndex = params.indexOf(' ');

      if (spaceIndex != -1)
      {
        String targetTag = params.substring(0, spaceIndex);
        String novoNome = params.substring(spaceIndex + 1);

        if (DB_RenameUser(targetTag, novoNome))
        {
          Serial.print(F("[OK] Nome atualizado! Nova identidade: "));
          Serial.println(novoNome);
        }
        else
        {
          Serial.println(F("[ERRO] Tag nao encontrada na EEPROM."));
        }
      }
      else
      {
        Serial.println(F("[ERRO] Sintaxe incorreta. Use: setName TAG NOME"));
      }
    }
    // Comando inválido
    else if (comando.length() > 0)
    {
      Serial.println(F("[ERRO] Comando desconhecido. Digite 'help' para ver a lista."));
    }
  }

  // --- HEALTH CHECK DO RFID ---
  if (millis() - lastHealthCheck > TEMPO_HEALTH_CHECK)
  {
    lastHealthCheck = millis();
    HAL_RFID_HealthCheck();
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
      else
      {
        // A MAGICA ACONTECE AQUI: Pergunta ao banco de dados quem é a tag
        String nomeUsuario = DB_IdentifyUser(tagLidaAgora);

        if (nomeUsuario != "")
        {
          Serial.print(F("[FSM:IDLE] Acesso Liberado: "));
          Serial.println(nomeUsuario);

          // >>> GRAVA O LOG AQUI <<<
          addLog(nomeUsuario);

          setState(STATE_DOOR_OPEN);
        }
        else
        {
          Serial.println(F("[FSM:IDLE] Tag = DESCONHECIDA."));
          setState(STATE_ACCESS_DENIED);
        }
      }
    }
    break;

  case STATE_DOOR_OPEN:
    if (millis() - stateTimer > TEMPO_PORTA_ABERTA)
    {
      HAL_GPIO_RelayClose();
      setState(STATE_IDLE);
    }
    break;

  case STATE_ACCESS_DENIED:
    if (millis() - stateTimer > TEMPO_ACESSO_NEGADO)
    {
      HAL_GPIO_LedDeniedOff();
      setState(STATE_IDLE);
    }
    break;

  case STATE_SET_MASTER:
    if (pollCartao())
    {
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
    if (millis() - stateTimer > TEMPO_ADMIN_TIMEOUT)
    {
      Serial.println(F("[FSM:ADMIN] Timeout atingido."));
      setState(STATE_IDLE);
      break;
    }

    if (pollCartao())
    {
      if (tagLidaAgora.equalsIgnoreCase(masterTagID))
      {
        Serial.println(F("[FSM:ADMIN] Mestre apresentado novamente. Saindo..."));
        systemBeep(1);
        setState(STATE_IDLE);
      }
      else
      {
        String nomeUsuario = DB_IdentifyUser(tagLidaAgora);

        if (nomeUsuario != "")
        {
          // A tag já existe! Vamos tentar remover da EEPROM.
          Serial.print(F("[FSM:ADMIN] Tag identificada: "));
          Serial.println(nomeUsuario);

          if (DB_RemoveUser(tagLidaAgora))
          {
            Serial.println(F("[FSM:ADMIN] Acao: REMOVER tag da EEPROM."));
            lcd.clear();
            lcd.print("Tag Removida!");
            systemBeep(1);
            HAL_GPIO_SafeDelay(200);
            systemBeep(1);
          }
          else
          {
            // Se não conseguiu remover da EEPROM, mas ela existe, é LEGADA!
            Serial.println(F("[FSM:ADMIN] Erro: Tentativa de alterar tag legada."));
            lcd.clear();
            lcd.print("Tag Protegida!");
            systemBeep(2);
          }
        }
        else
        {
          // A tag não existe em nenhum lugar. Vamos ADICIONAR.
          Serial.println(F("[FSM:ADMIN] Acao: ADICIONAR nova tag."));
          if (DB_AddUser(tagLidaAgora))
          {
            lcd.clear();
            lcd.print("Tag Adicionada!");
            systemBeep(3);
          }
          else
          {
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
void setState(SystemState newState)
{
  currentState = newState;
  switch (newState)
  {
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

void pollBotao()
{
  boolean estadoAtualBT = HAL_GPIO_ReadButton();
  if (estadoAtualBT == true && ultimoEstadoBT == false)
  {
    delay(20);
    if (HAL_GPIO_ReadButton() == true)
    {
      setState(STATE_DOOR_OPEN);
    }
  }
  ultimoEstadoBT = estadoAtualBT;
}

bool pollCartao()
{
  if (HAL_RFID_ReadCard(tagLidaAgora))
  {
    return true;
  }
  return false;
}

void systemBeep(int quantidade)
{
  HAL_GPIO_Beep(quantidade);
  delay(50);
  HAL_RFID_WakeUp();
}
