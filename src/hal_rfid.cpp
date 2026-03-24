#include "hal_rfid.h"
#include <SPI.h>
#include <MFRC522.h>

// Escondemos os pinos do SPI e do módulo aqui dentro!
#define SS_PIN 10
#define RST_PIN 9

// A instância do leitor agora é privada deste arquivo
MFRC522 leitorRFID(SS_PIN, RST_PIN);

void HAL_RFID_Init() {
  SPI.begin();
  leitorRFID.PCD_Init();
}

void HAL_RFID_WakeUp() {
  // Reinicia apenas a antena/configuração (ideal para pós-ruído de relé)
  leitorRFID.PCD_Init(); 
}

void HAL_RFID_HealthCheck() {
  byte versao = leitorRFID.PCD_ReadRegister(leitorRFID.VersionReg);
  
  if (versao == 0x00 || versao == 0xFF) {
    Serial.println(F("[ERRO CRITICO] Módulo RFID travou (Falha SPI)! Tentando recuperar..."));
    SPI.begin(); // As vezes o barramento SPI inteiro cai
    leitorRFID.PCD_Init();
    delay(50);
    Serial.println(F("[DEBUG] Modulo RFID reinicializado."));
  }
}

bool HAL_RFID_ReadCard(String &uidOut) {
  // Verifica se há um novo cartão e se consegue ler o serial dele
  if (!leitorRFID.PICC_IsNewCardPresent() || !leitorRFID.PICC_ReadCardSerial()) {
    return false;
  }

  uidOut = "";
  // Monta a String com zeros à esquerda para manter o padrão "c42555d3"
  for (byte i = 0; i < leitorRFID.uid.size; i++) {
    if (leitorRFID.uid.uidByte[i] < 0x10) {
      uidOut += "0";
    }
    uidOut += String(leitorRFID.uid.uidByte[i], HEX);
  }

  // Para a comunicação com o cartão atual e limpa a criptografia para liberar o SPI
  leitorRFID.PICC_HaltA();
  leitorRFID.PCD_StopCrypto1(); 

  return true;
}