#ifndef __OLED_H__
#define __OLED_H__

#include "spi.h"

void orchardGfxInit(void);
void orchardGfxStart(void);
void orchardGfxEnd(void);

void oledPauseBanner(const char *str);

void oledStart(SPIDriver *device);
void oledStop(SPIDriver *device);
void oledAcquireBus(void);
void oledReleaseBus(void);
void oledCmd(uint8_t cmd);
void oledData(uint8_t *data, uint16_t length);
void oledOrchardBanner(void);
void oledTestBanner(char *str);
void oledTestPattern(uint8_t pattern);

#endif /* __OLED_H__ */
