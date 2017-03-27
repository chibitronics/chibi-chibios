#include "ch.h"
#include "hal.h"

#include "orchard.h"
#include "orchard-shell.h"
#include "orchard-events.h"
#include "oled.h"
#include "gfx.h"

#include "analog.h"

#include "dv-oscope.h"

uint8_t speed_mode = 0;
uint8_t cmp_init = 0;
extern event_source_t cmp_event;
extern uint8_t current_mode;

static void draw_wave(uint16_t *samples) {
  coord_t width;
  coord_t height;
  int i;
  uint16_t min, max, offset;
  uint32_t temp;

  width = gdispGetWidth();
  height = gdispGetHeight();

  min = 0xFFFF, max = 0x0;
  for( i = 0; i < SCOPE_SAMPLE_DEPTH; i++ ) {
    if( samples[i] > max )
      max = samples[i];
    if( samples[i] < min )
      min = samples[i];
  }

  if( max == 0 )
    max = 1;  // to avoid divide by zero

  if( (((uint32_t)(max - min) * (uint32_t) height) / (uint32_t) max) < (uint32_t) height )
    offset = (height - (((uint32_t)(max - min) * (uint32_t) height) / (uint32_t) max)) / 2;
  else
    offset = 0;

  for( i = 0; i < SCOPE_SAMPLE_DEPTH; i++ ) {
    // zero-reference
    samples[i] -= min;
    
    // now scale to screen height
    temp = (uint32_t) samples[i];
    temp *= (uint32_t) height;
    temp /= max;
    samples[i] = temp;

    samples[i] += offset;
  }

  orchardGfxStart();
  gdispClear(Black);

  for( i = 0; i < width - 1; i++ ) {
    gdispDrawLine( i, height - 1 - samples[i], i+1, height - 1 - samples[i+1], White );
  }
  
  gdispFlush();
  orchardGfxEnd();
}

void updateOscopeScreen(void) {
  uint16_t *samples;

  if( current_mode != MODE_OSCOPE )
    return;
  
  // grab a reading
  samples = scopeRead(0, speed_mode);  // 1 is high, 0 is low
  
  // now display the time-averaged voltage
  draw_wave(samples);
}

void cmp_handler(eventid_t id) {
  (void) id;

  // add some trigger holdoff code here
  updateOscopeScreen();
}

static void serve_cmp_interrupt(CMP_TypeDef *cmp) {
  (void) cmp;
  
  osalSysLockFromISR();
  if( cmp_init )
    chEvtBroadcastI(&cmp_event); 
  osalSysUnlockFromISR();
}

OSAL_IRQ_HANDLER(Vector80) {

  OSAL_IRQ_PROLOGUE();
  serve_cmp_interrupt(CMP0);
  OSAL_IRQ_EPILOGUE();
}

void oscopeInit(void) {
  // enable the DAC
  // VRSEL = 1 means to use Vin2 which is VDD. Don't use VrefH because it causes crosstalk with ADC.
  // set VOSEL = 31, so 31 + 1 / 64 = 0.5 * VDD, e.g. midpoint select for CMP.
  CMP0->DACCR = CMP_DACCR_DACEN(1) | CMP_DACCR_VRSEL(1) | CMP_DACCR_VOSEL(31);

  CMP0->MUXCR = CMP_MUXCR_MSEL(7) | CMP_MUXCR_PSEL(0);  // 0 = CMP0_IN0, 7 = 6-bit DAC0 reference

  // now we are configured to trip 1 when ADC input is above 1.5V

  CMP0->CR0 = CMP_CR0_HYSTCTR(3) | CMP_CR0_FILTER_CNT(1); // 1 consecutive samples to agree
  CMP0->CR1 = CMP_CR1_EN(1) | CMP_CR1_PMODE(1);
  CMP0->FPR = CMP_FPR_FILT_PER(2); // filter at a rate of 12MHz (busclk = 24MHz / 2)

  nvicEnableVector(CMP0_IRQn, KINETIS_CMP0_PRIORITY);
  
  chEvtObjectInit(&cmp_event);
  cmp_init = 1;
}
