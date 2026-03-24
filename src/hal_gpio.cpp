#include "hal_gpio.h"
#include "hal_wdt.h" // Precisamos do WDT aqui para o SafeDelay

void HAL_GPIO_Init() {
  pinMode(PIN_RELE, OUTPUT);
  pinMode(PIN_LED_NEGADO, OUTPUT);
  pinMode(PIN_BOTAO_ABRIR, INPUT_PULLUP);

  digitalWrite(PIN_RELE, LOW);
  digitalWrite(PIN_LED_NEGADO, LOW);
}

bool HAL_GPIO_ReadButton() {
  // Retorna true se o botão foi pressionado (LOW por causa do PULLUP)
  return digitalRead(PIN_BOTAO_ABRIR) == LOW;
}

void HAL_GPIO_RelayOpen() {
  digitalWrite(PIN_RELE, HIGH);
}

void HAL_GPIO_RelayClose() {
  digitalWrite(PIN_RELE, LOW);
}

void HAL_GPIO_LedDeniedOn() {
  digitalWrite(PIN_LED_NEGADO, HIGH);
}

void HAL_GPIO_LedDeniedOff() {
  digitalWrite(PIN_LED_NEGADO, LOW);
}

void HAL_GPIO_SafeDelay(unsigned long ms) {
  unsigned long start = millis();
  while (millis() - start < ms) {
    HAL_WDT_Feed(); // Usa a nossa nova HAL!
    delay(1);
  }
}

void HAL_GPIO_Beep(int count) {
  for (int i = 0; i < count; i++) {
    HAL_GPIO_RelayOpen(); // Usa o relé como buzzer
    HAL_GPIO_SafeDelay(200);
    HAL_GPIO_RelayClose();
    if (i < count - 1) {
      HAL_GPIO_SafeDelay(100);
    }
  }
  // Removemos o PCD_Init() daqui. Essa lógica de reiniciar a antena 
  // do RFID será feita pela máquina de estados ou pela HAL do RFID depois!
}