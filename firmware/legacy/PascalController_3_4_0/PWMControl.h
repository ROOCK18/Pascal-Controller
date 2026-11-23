// ============================================================
// PWMControl.h - PWM kimenetek beállítása és az óra léptetése
// ============================================================
#pragma once

// ============================================================
// ÓRA / PWM
// ============================================================
void updateClock() {
  if (millis() - lastClockUpdate >= 1000) {
    lastClockUpdate = millis();
    sec++;
    if (sec >= 60) { sec = 0; min_t++; }
    if (min_t >= 60) { min_t = 0; hr++; }
    if (hr >= 24) hr = 0;
  }
}

void setupPWM() {
  ledcAttach(RGB_R_PIN, LED_PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(RGB_G_PIN, LED_PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(RGB_B_PIN, LED_PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(FAN1_PWM_PIN, FAN1_PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(FAN2_PWM_PIN, FAN2_PWM_FREQ, PWM_RESOLUTION);
  ledcWrite(RGB_R_PIN, 0); ledcWrite(RGB_G_PIN, 0); ledcWrite(RGB_B_PIN, 0);
  ledcWrite(FAN1_PWM_PIN, 0); ledcWrite(FAN2_PWM_PIN, 0);
}

void setPWM(int channel, int duty) {
  duty = constrain(duty, 0, 255);
  switch (channel) {
    case 0: pwmR = duty; ledcWrite(RGB_R_PIN, duty); break;
    case 1: pwmG = duty; ledcWrite(RGB_G_PIN, duty); break;
    case 2: pwmB = duty; ledcWrite(RGB_B_PIN, duty); break;
    case 3: pwmFan = duty; ledcWrite(FAN1_PWM_PIN, duty); break;
    case 4: pwmFan2 = duty; ledcWrite(FAN2_PWM_PIN, duty); break;
  }
}

void updatePWMOutputs() {
  setPWM(0, pwmR); setPWM(1, pwmG); setPWM(2, pwmB);
  setPWM(3, pwmFan); setPWM(4, pwmFan2);
}
