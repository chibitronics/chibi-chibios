/*
    ChibiOS - Copyright (C) 2006..2015 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "ch.h"
#include "hal.h"
#include "i2c.h"
#include "pal.h"
#include "adc.h"

#include "shell.h"
#include "chprintf.h"

#include "orchard.h"
#include "orchard-shell.h"
#include "orchard-events.h"

#include "oled.h"
#include "analog.h"

#include "gfx.h"

#include "dv-serialmode.h"
#include "dv-volts.h"
#include "dv-oscope.h"

#include "kl02x.h"

struct evt_table orchard_app_events;
event_source_t refresh_event;
event_source_t serial_event;
event_source_t mode_event;
event_source_t option_event;
event_source_t led_event;
event_source_t cmp_event;
uint8_t serial_init = 0;

static virtual_timer_t led_vt;
static virtual_timer_t refresh_vt;

static void mode_cb(EXTDriver *extp, expchannel_t channel);
static void option_cb(EXTDriver *extp, expchannel_t channel);

uint32_t serial_needs_update = 0;

#define PWM_MAX 4096
#define PWM_INCREMENT 140
static const PWMConfig pwm_config = {
  47972352UL / 16, // frequency of PWM clock
  PWM_MAX,    // # of PWM clocks per PWM period
  NULL,    // callback
  {{PWM_OUTPUT_ACTIVE_HIGH, NULL}, {PWM_OUTPUT_ACTIVE_HIGH, NULL}},
};

static const SPIConfig spi_config = {
  NULL,
  /* HW dependent part.*/
  GPIOA,
  5,
};

static const ADCConfig adccfg1 = {
  /* Perform initial calibration */
  true
};

static const EXTConfig ext_config = {
  {
    {EXT_CH_MODE_FALLING_EDGE | EXT_CH_MODE_AUTOSTART, mode_cb, PORTB, 6},
    {EXT_CH_MODE_FALLING_EDGE | EXT_CH_MODE_AUTOSTART, option_cb, PORTB, 7},
  }
};

uint8_t current_mode = MODE_SERIAL;

static pwmcnt_t pwm_width = 0;
static uint32_t pwm_counting_up = 1;
#define REFRESH_RATE  50   // in ms

static void led_cb(void *arg) {
  (void) arg;

  // palTogglePad(GPIOA, 6);
  chSysLockFromISR();
  chEvtBroadcastI(&led_event);
  chVTSetI(&led_vt, MS2ST(REFRESH_RATE), led_cb, NULL);
  chSysUnlockFromISR();
}

static void refresh_cb(void *arg) {
  (void) arg;

  chSysLockFromISR();
  chVTSetI(&refresh_vt, MS2ST(REFRESH_RATE), refresh_cb, NULL);
  chEvtBroadcastI(&refresh_event);
  chSysUnlockFromISR();
}

static void mode_cb(EXTDriver *extp, expchannel_t channel) {
  (void)extp;
  (void)channel;
  if(palReadPad(IOPORT2, 7) == PAL_HIGH) { // other button isn't alread pressed
    chSysLockFromISR();
    chEvtBroadcastI(&mode_event);
    chSysUnlockFromISR();
  }
}


static void option_cb(EXTDriver *extp, expchannel_t channel) {
  (void)extp;
  (void)channel;

  // check to see if the other button is already held down before issuing event
  if(palReadPad(IOPORT2, 6) == PAL_HIGH) { // other button isn't already pressed
    chSysLockFromISR();
    chEvtBroadcastI(&option_event);
    chSysUnlockFromISR();
  }
}

uint8_t oled_pattern = 0;
uint8_t option_handler_hit = 0;
uint8_t mode_handler_hit = 0;

static void led_handler(eventid_t id) {
  (void) id;

  PWMD1.tpm->C[0].V = pwm_width;
  
  if( pwm_counting_up ) {
    pwm_width += PWM_INCREMENT;
    if( pwm_width > (PWM_MAX - 1) ) {
      pwm_width = PWM_MAX - 1;
      pwm_counting_up = 0;
    }
  } else {
    if( pwm_width > PWM_INCREMENT ) {
      pwm_width -= PWM_INCREMENT;
    } else {
      pwm_width = 0;
      pwm_counting_up = 1;
    }
  }

}

static void refresh_handler(eventid_t id) {
  (void) id;

  switch(current_mode) {
  case MODE_SERIAL:
    if( serial_needs_update ||
	((chVTTimeElapsedSinceX( last_update_time ) > AUTO_UPDATE_TIMEOUT_ST) && !locker_mode) ) {
      serial_needs_update = 0;
      updateSerialScreen();
    }
    break;
  case MODE_VOLTS:
    updateVoltsScreen();
    break;
  case MODE_OSCOPE:
    if( chVTTimeElapsedSinceX( last_trigger_time ) > AUTOSAMPLE_HOLDOFF_ST ) {
      adcAcquireBus(&ADCD1);      
      osalSysLock();
      scopeReadI();
      osalSysUnlock();
      adcReleaseBus(&ADCD1);
    }
    // add code to enable auto-sampling if trigger has not been found for a while
    // updateOscopeScreen(); // now handled by trigger mechanism
    break;
  default:
    break;
  }
}

#define ENOUGH_HITS 8

static void option_handler(eventid_t id) {
  (void) id;

  if(current_mode == MODE_TEST) {
    oled_pattern++;
    oledTestPattern(oled_pattern);
    option_handler_hit ++;
    if( option_handler_hit != 0 && mode_handler_hit != 0 ) {
      if( option_handler_hit + mode_handler_hit > ENOUGH_HITS ) {
	chprintf(stream, "PASS\n\r");
      }
    }
  }
}

static void serial_handler(eventid_t id) {
  (void) id;
  dvDoSerial();
}

static void mode_handler(eventid_t id) {
  (void) id;
  
  if(current_mode == MODE_TEST) {
    oled_pattern++;
    oledTestPattern(oled_pattern);
    mode_handler_hit ++;
    if( option_handler_hit != 0 && mode_handler_hit != 0 ) {
      if( option_handler_hit + mode_handler_hit > ENOUGH_HITS ) {
	chprintf(stream, "PASS\n\r");
      }
    }
  }
}

static thread_t *evHandler_tp = NULL;
static THD_WORKING_AREA(waEvHandlerThread, 0x480);

static THD_FUNCTION(evHandlerThread, arg) {
  (void)arg;
  chRegSetThreadName("Event dispatcher");

  adcStart(&ADCD1, &adccfg1);
  analogStart();
  oscopeInit();

  spiStart(&SPID1, &spi_config);
  oledStart(&SPID1);

  orchardShellInit();

  chprintf(stream, "\r\nChibiscreen build %s\r\n", gitversion);
  chprintf(stream, "Copyright (c) 2016 Chibitronics PTE LTD\r\n", gitversion);
  chprintf(stream, "boot freemem: %d\r\n", chCoreGetStatusX());

  evtTableInit(orchard_app_events, 6);

  chEvtObjectInit(&mode_event);
  evtTableHook(orchard_app_events, mode_event, mode_handler);

  chEvtObjectInit(&option_event);
  evtTableHook(orchard_app_events, option_event, option_handler);

  chEvtObjectInit(&refresh_event);
  evtTableHook(orchard_app_events, refresh_event, refresh_handler);

  chEvtObjectInit(&serial_event);
  current_mode = MODE_SERIAL;
  evtTableHook(orchard_app_events, serial_event, serial_handler);

  chEvtObjectInit(&led_event);
  evtTableHook(orchard_app_events, led_event, led_handler);

  // event object initialization happens in oscopeIinit
  evtTableHook(orchard_app_events, cmp_event, cmp_handler);
  
  extStart(&EXTD1, &ext_config); // enables interrupts on gpios

  // start LED flashing
  pwmStart(&PWMD1, &pwm_config);
  pwmEnableChannel(&PWMD1, 0, 0);
  //  palSetPadMode(IOPORT1, 6, PAL_MODE_ALTERNATIVE_2); // now handled in board.c
  chVTObjectInit(&led_vt);
  chVTSet(&led_vt, MS2ST(500), led_cb, NULL);

  // now handled properly elsewhere
  // palWritePad(GPIOA, 5, PAL_HIGH);  // oled_cs
  // palWritePad(GPIOB, 13,PAL_HIGH);  // oled_dc

  //  palWritePad(GPIOB, 11,PAL_HIGH);  // oled_res
  orchardGfxInit();
  // oledOrchardBanner();
  
  chprintf(stream, "after gfx mem: %d bytes\r\n", chCoreGetStatusX());

  // start refreshing the screen
  chVTObjectInit(&refresh_vt);
  chVTSet(&refresh_vt, MS2ST(REFRESH_RATE), refresh_cb, NULL);

  serial_init = 1;
  nvicEnableVector(UART0_IRQn, KINETIS_SERIAL_UART0_PRIORITY);
  oledTestBanner("Waiting for SERIAL");
  chprintf(stream, "TEST START\r\n");

  dvInit();
  while(true) {
    chEvtDispatch(evtHandlers(orchard_app_events), chEvtWaitOne(ALL_EVENTS));
    //    if( current_mode == MODE_SERIAL )
    //      dvDoSerial(); // this grabs characters and processes them
  }
}

/*
 * Application entry point.
 */
int main(void)
{
  /*
   * System initializations.
   * - HAL initialization, this also initializes the configured device drivers
   *   and performs the board-specific initializations.
   * - Kernel initialization, the main() function becomes a thread and the
   *   RTOS is active.
   */
  halInit();
  chSysInit();

  // set this low here in case something crashes later on, at least we have the LED on to indicate power-on
  palWritePad(GPIOA, 6, PAL_LOW); // mcu_led

  evHandler_tp = chThdCreateStatic(waEvHandlerThread, sizeof(waEvHandlerThread), NORMALPRIO + 10, evHandlerThread, NULL);

  while (TRUE) {
    // this is now an idle loop
  }

}

