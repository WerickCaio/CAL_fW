#ifndef HAL_RFID_H
#define HAL_RFID_H

#include <Arduino.h>

// --- PROTÓTIPOS DA CAMADA RFID ---
void HAL_RFID_Init();
void HAL_RFID_WakeUp();           // O nosso "remédio" contra o ruído do relé
void HAL_RFID_HealthCheck();      // Verifica se o barramento SPI travou e recupera
bool HAL_RFID_ReadCard(String &uidOut); // Lê o cartão e devolve o ID na String passada
void HAL_RFID_ForceReset();

#endif