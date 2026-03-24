#include "database.h"
#include <EEPROM.h>

#define TAG_ID_LENGTH 16    
#define TAG_NAME_LENGTH 16  
#define SLOT_SIZE 32        
#define MASTER_SLOT_ADDR 0  
#define USER_SLOTS_START 1  
#define MAX_EEPROM_USERS 31 

// =========================================================================
// A GRANDE CORREÇÃO DE RAM: Usando const char* em vez de String!
// Isso economiza centenas de bytes de RAM e impede o travamento.
// =========================================================================
struct LegacyUser {
    const char* tag;
    const char* name;
};

const LegacyUser TagsCadastradas[] = {
    {"c42555d3", "Prof. Lorena"},
    {"3312a53", "Prof. Andre"},
    {"3ae17517", "Prof. Auzuir"},
    {"79cf30c3", "Prof. Eloy"},
    {"7a6ca07f", "P. Auzuir"},
    {"144c67a3", "Prof. Nelio"},
    {"d929e5b9", "Prof. Andre (G)"}, 
    {"f992c5b8", "Prof. Rodrigo"},
    {"16454e99", "Vania"},
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

String readTagFromAddr(int addr) {
  String tag = "";
  byte firstByte = EEPROM.read(addr);
  if (firstByte == 0xFF || firstByte == 0x00) return ""; 

  for (int i = 0; i < TAG_ID_LENGTH; i++) {
    byte b = EEPROM.read(addr + i);
    if (b == 0xFF || b == 0x00) break; 
    tag += (char)b;
  }
  return tag;
}

String readNameFromAddr(int addr) {
  String name = "";
  byte firstByte = EEPROM.read(addr + TAG_ID_LENGTH);
  if (firstByte == 0xFF || firstByte == 0x00) return "Desconhecido";

  for (int i = 0; i < TAG_NAME_LENGTH; i++) {
    byte b = EEPROM.read(addr + TAG_ID_LENGTH + i);
    if (b == 0xFF || b == 0x00) break;
    name += (char)b;
  }
  return name;
}

void writeUserToAddr(int addr, String tag, String name) {
  Serial.print(F("[DB-DEBUG] Gravando no endereco: ")); Serial.print(addr);
  Serial.print(F(" | Tag recebida: '")); Serial.print(tag); Serial.println(F("'"));
  
  // TRAVA DE SEGURANÇA: Impede gravar vazio se a RAM falhar!
  if (tag.length() == 0) {
    Serial.println(F("[DB-ERRO CRITICO] A Tag chegou vazia! Gravacao abortada."));
    return;
  }

  const char* tagCStr = tag.c_str();
  for (int i = 0; i < TAG_ID_LENGTH; i++) {
    byte b = (i < tag.length()) ? tagCStr[i] : 0x00;
    EEPROM.update(addr + i, b);
  }
  
  const char* nameCStr = name.c_str();
  for (int i = 0; i < TAG_NAME_LENGTH; i++) {
    byte b = (i < name.length()) ? nameCStr[i] : 0x00;
    EEPROM.update(addr + TAG_ID_LENGTH + i, b);
  }

  byte verificationByte = EEPROM.read(addr);
  Serial.print(F("[DB-DEBUG] Byte 0 verificado: 0x"));
  Serial.println(verificationByte, HEX);
}

void eraseSlot(int addr) {
  for (int i = 0; i < SLOT_SIZE; i++) EEPROM.update(addr + i, 0xFF);
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
    byte checkByte = EEPROM.read(addr);
    
    Serial.print(F("[DB-DEBUG] Checando vaga no end. ")); Serial.print(addr);
    Serial.print(F(" | Byte 0 = 0x")); Serial.println(checkByte, HEX);

    // Como corrigimos a gravacao vazia, 0x00 ou 0xFF realmente significa vazio agora!
    if (checkByte == 0xFF || checkByte == 0x00) {
      Serial.println(F("[DB-DEBUG] -> VAGA ENCONTRADA!"));
      return addr;
    }
  }
  Serial.println(F("[DB-DEBUG] -> MEMORIA CHEIA!"));
  return -1;
}

String DB_LoadMaster() { return readTagFromAddr(MASTER_SLOT_ADDR); }

void DB_SaveMaster(String tag) { 
  Serial.println(F("[DB] Salvando novo Master..."));
  writeUserToAddr(MASTER_SLOT_ADDR, tag, "MASTER"); 
}

String DB_IdentifyUser(String tag) {
  int totalLegacy = sizeof(TagsCadastradas) / sizeof(LegacyUser);
  for (int i = 0; i < totalLegacy; i++) {
    if (tag.equalsIgnoreCase(String(TagsCadastradas[i].tag))) {
      return String(TagsCadastradas[i].name);
    }
  }
  int addr = findTagInEEPROM(tag);
  if (addr != -1) return readNameFromAddr(addr); 
  return ""; 
}

bool DB_AddUser(String tag) {
  int emptyAddr = findEmptySlot();
  if (emptyAddr == -1) return false; 
  
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
  if (novoNome.length() > 15) novoNome = novoNome.substring(0, 15); 
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