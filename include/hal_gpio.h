#ifndef HAL_GPIO_H
#define HAL_GPIO_H

#include <Arduino.h>

// --- DEFENIÇÕES DE PINOS ---
// O main.cpp não precisa mais saber os números dos pinos!
#define PIN_BOTAO_ABRIR 5
#define PIN_RELE        3
#define PIN_LED_NEGADO  2

// --- PROTÓTIPOS ---
void HAL_GPIO_Init();
bool HAL_GPIO_ReadButton();
void HAL_GPIO_RelayOpen();
void HAL_GPIO_RelayClose();
void HAL_GPIO_LedDeniedOn();
void HAL_GPIO_LedDeniedOff();
void HAL_GPIO_Beep(int count);
void HAL_GPIO_SafeDelay(unsigned long ms); // Substitui seu safeDelay antigo

#endif