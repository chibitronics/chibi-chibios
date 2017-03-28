extern uint8_t speed_mode;
extern systime_t last_trigger_time;

void updateOscopeScreen(void);
void oscopeInit(void);
void cmp_handler(eventid_t id);

#define AUTOSAMPLE_HOLDOFF_ST MS2ST(1000)
