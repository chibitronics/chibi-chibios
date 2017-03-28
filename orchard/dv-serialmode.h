extern systime_t last_update_time;
extern uint8_t locker_mode;

void dvInit(void);
void dvDoSerial(void);
void updateSerialScreen(void);

#define AUTO_UPDATE_TIMEOUT_ST MS2ST(500)
