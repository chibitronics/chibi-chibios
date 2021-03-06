#include "hal.h"

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief   Unhandled exceptions handler.
 * @details Any undefined exception vector points to this function by default.
 *          This function simply stops the system into an infinite loop.
 *
 * @notapi
 */
/*lint -save -e9075 [8.4] All symbols are invoked from asm context.*/

register void *stack_pointer asm("sp");

/* When an exception occurs, the hardware "stacks" these values:*/
struct arm_context {
  uint32_t r0;
  uint32_t r1;
  uint32_t r2;
  uint32_t r3;
  uint32_t r12;
  void (*lr)(void);
  uint32_t *pc;
  uint32_t xpsr;
};

/*lint -save -e9075 [8.4] All symbols are invoked from asm context.*/
void HardFault_Handler_C(struct arm_context *context, bool is_irq) {
/*lint -restore*/
  (void)context;
  (void)is_irq; /* Set to 1 if called from an IRQ context */
  asm("bkpt #0");
}

/*lint -save -e9075 [8.4] All symbols are invoked from asm context.*/
void MemManage_Handler_C(struct arm_context *context, bool is_irq) {
/*lint -restore*/
  (void)context;
  (void)is_irq; /* Set to 1 if called from an IRQ context */
  /* Break into the debugger */
  asm("bkpt #0");
}

/*lint -save -e9075 [8.4] All symbols are invoked from asm context.*/
void BusFault_Handler_C(struct arm_context *context, bool is_irq) {
/*lint -restore*/
  (void)context;
  (void)is_irq; /* Set to 1 if called from an IRQ context */
  asm("bkpt #0");
}

/*lint -save -e9075 [8.4] All symbols are invoked from asm context.*/
void UsageFault_Handler_C(struct arm_context *context, bool is_irq) {
/*lint -restore*/
  (void)context;
  (void)is_irq; /* Set to 1 if called from an IRQ context */
  asm("bkpt #0");
}

uintptr_t __stack_chk_guard = 0x12345678;
__attribute__((noreturn))
void __stack_chk_fail(void) {
  chSysHalt("Stack check fail");
  while(1);
}

