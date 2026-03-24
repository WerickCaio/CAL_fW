#include "hal_wdt.h"
#include <avr/wdt.h> // A biblioteca específica do hardware fica ESCONDIDA aqui!
#include <Arduino.h>

void HAL_WDT_Disable() {
  MCUSR = 0; // Limpa as flags de reset (Essencial para AVR)
  wdt_disable();
}

void HAL_WDT_Enable() {
  wdt_enable(WDTO_8S); // Configurado para 8 segundos como no seu código original
}

void HAL_WDT_Feed() {
  wdt_reset();
}