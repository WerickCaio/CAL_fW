#include "database.h"
#include <EEPROM.h>

// =========================================================================
// CONSTANTES DA EEPROM (32 Bytes por Slot)
// =========================================================================
#define TAG_ID_LENGTH 16    
#define TAG_NAME_LENGTH 16  
#define SLOT_SIZE 32        // Tag (16 bytes) + Nome (16 bytes)
#define MASTER_SLOT_ADDR 0  
#define USER_SLOTS_START 1  
#define MAX_EEPROM_USERS 31 // (1024 - 32) / 32 = 31 vagas na EEPROM

// =========================================================================
// BANCO DE DADOS FIXO (Array Legado)
// =========================================================================
struct LegacyUser {
    String tag;
    String name;
};

LegacyUser TagsCadastradas[] = {
    // Professores
    {"c42555d3", "Prof. Lorena"},
    {"3312a53", "Prof. Andre"},
    {"3ae17517", "Prof. Auzuir"},
    {"79cf30c3", "Prof. Eloy"},
    {"7a6ca07f", "P. Auzuir"},
    {"144c67a3", "Prof. Nelio"},
    {"d929e5b9", "Prof. Andre (G)"}, 
    {"f992c5b8", "Prof. Rodrigo"},

    // Colaboradores
    {"16454e99", "Vania"},

    // Bolsistas
    {"9b2d4b9", "Joao Paulo"},
    {"44f75f2da7780", "Werick 2024"},
    {"444492257980", "Gabriel 2024"},
    {"439476a387980", "Joao Paulo Cart"},
    {"42ed2257980", "Pedro Henrique"},
    {"43b3b5a387980", "Marina"},
    {"471675a387980", "Pedro H. (2)"},   
    {"45f2ca3a7980", "Gabriel 2025"},
    {"426515a387980", "Livia"},
    {"439425a387980", "Aquiles"}
};

// =========================================================================
// FUNÇÕES INTERNAS PRIVADAS (O main.cpp não vê isso)
// =========================================================================

String readTagFromAddr(int addr) {
  char tagChars[TAG_ID_LENGTH];
  EEPROM.get(addr, tagChars);
  if (tagChars[0] == (char)0xFF || tagChars[0] == 0x00) return "";
  tagChars[TAG_ID_LENGTH - 1] = '\0';
  return String(tagChars);
}

String readNameFromAddr(int addr) {
  char nameChars[TAG_NAME_LENGTH];
  EEPROM.get(addr + TAG_ID_LENGTH, nameChars); 
  if (nameChars[0] == (char)0xFF || nameChars[0] == 0x00) return "Desconhecido";
  nameChars[TAG_NAME_LENGTH - 1] = '\0';
  return String(nameChars);
}

void writeUserToAddr(int addr, String tag, String name) {
  char buffer[SLOT_SIZE];
  memset(buffer, 0, SLOT_SIZE); // Limpa o buffer com zeros
  
  tag.toCharArray(buffer, TAG_ID_LENGTH); 
  name.toCharArray(buffer + TAG_ID_LENGTH, TAG_NAME_LENGTH); 
  
  EEPROM.put(addr, buffer);
  Serial.print(F("[DB] Usuario salvo no endereco: "));
  Serial.println(addr);
}

void eraseSlot(int addr) {
  char emptySlot[SLOT_SIZE];
  memset(emptySlot, 0xFF, SLOT_SIZE);
  EEPROM.put(addr, emptySlot);
  Serial.print(F("[DB] Slot apagado no endereco: "));
  Serial.println(addr);
}

int findTagInEEPROM(String tag) {
  for (int i = 0; i < MAX_EEPROM_USERS; i++) {
    int addr = (USER_SLOTS_START + i) * SLOT_SIZE;
    if (tag.equalsIgnoreCase(readTagFromAddr(addr))) return addr;
  }
  return -1;
}

int findEmptySlot() {
  for (int i = 0; i < MAX_EEPROM_USERS; i++) {
    int addr = (USER_SLOTS_START + i) * SLOT_SIZE;
    if (EEPROM.read(addr) == 0xFF || EEPROM.read(addr) == 0x00) return addr;
  }
  return -1;
}

// =========================================================================
// FUNÇÕES PÚBLICAS (As que o main.cpp usa)
// =========================================================================

String DB_LoadMaster() { 
  return readTagFromAddr(MASTER_SLOT_ADDR); 
}

void DB_SaveMaster(String tag) { 
  Serial.println(F("[DB] Salvando novo Master..."));
  writeUserToAddr(MASTER_SLOT_ADDR, tag, "MASTER"); 
}

String DB_IdentifyUser(String tag) {
  // 1. Procura no Array Legado
  int totalLegacy = sizeof(TagsCadastradas) / sizeof(LegacyUser);
  for (int i = 0; i < totalLegacy; i++) {
    if (tag.equalsIgnoreCase(TagsCadastradas[i].tag)) {
      return TagsCadastradas[i].name;
    }
  }

  // 2. Procura na EEPROM
  int addr = findTagInEEPROM(tag);
  if (addr != -1) {
    return readNameFromAddr(addr); // Retorna o nome que gravamos!
  }

  return ""; // Tag não encontrada
}

bool DB_AddUser(String tag) {
  int emptyAddr = findEmptySlot();
  if (emptyAddr == -1) return false; // Memória cheia
  
  // Descobre o número do slot para gerar o nome "Visitante XX"
  int slotIndex = (emptyAddr / SLOT_SIZE);
  String defaultName = "Visitante ";
  if (slotIndex < 10) defaultName += "0";
  defaultName += String(slotIndex);

  writeUserToAddr(emptyAddr, tag, defaultName);
  return true;
}

bool DB_RemoveUser(String tag) {
  int tagAddr = findTagInEEPROM(tag);
  if (tagAddr == -1) return false; 
  eraseSlot(tagAddr);
  return true;
}

bool DB_RenameUser(String tag, String novoNome) {
  int addr = findTagInEEPROM(tag);
  if (addr == -1) return false; 
  
  if (novoNome.length() > 15) {
    novoNome = novoNome.substring(0, 15); // Corta para caber na memória e no LCD
  }
  
  writeUserToAddr(addr, tag, novoNome);
  return true;
}

void DB_DumpToSerial() {
  int usedSlots = 0;
  int freeSlots = 0;

  Serial.println(F("\n========================================="));
  Serial.println(F("    DUMP DE USUARIOS (EEPROM)            "));
  Serial.println(F("========================================="));

  for (int i = 0; i < MAX_EEPROM_USERS; i++) {
    int addr = (USER_SLOTS_START + i) * SLOT_SIZE;
    String tagInSlot = readTagFromAddr(addr); 

    if (tagInSlot != "") {
      String nameInSlot = readNameFromAddr(addr);
      
      Serial.print(F("Slot ["));
      if (i + 1 < 10) Serial.print(F("0")); 
      Serial.print(i + 1);
      Serial.print(F("]: "));
      Serial.print(tagInSlot);
      Serial.print(F(" | Nome: "));
      Serial.println(nameInSlot);
      usedSlots++;
    } else {
      freeSlots++;
    }
  }

  Serial.println(F("-----------------------------------------"));
  Serial.print(F("Usuarios Cadastrados : ")); Serial.println(usedSlots);
  Serial.print(F("Espacos Livres       : ")); Serial.println(freeSlots);
  Serial.print(F("Capacidade Maxima    : ")); Serial.println(MAX_EEPROM_USERS);
  Serial.println(F("=========================================\n"));
}