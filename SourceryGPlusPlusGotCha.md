While trying to figure out why my ISR was not being run I found this post:

http://e2e.ti.com/support/microcontrollers/stellaris_arm_cortex-m3_microcontroller/f/471/t/45952.aspx


There are defaults for all ISRs when you use the G++ tool chain :-\
This applies to ALL of there tool chains and not just the pro versions... As I hit this using the lite version!!!!


<pre>
/* ISR names for stellaris<br>
*<br>
* Version:Sourcery G++ 4.3-58<br>
* BugURL:https://support.codesourcery.com/GNUToolchain/<br>
*<br>
* Copyright 2007, 2008 CodeSourcery, Inc.<br>
*<br>
* The authors hereby grant permission to use, copy, modify, distribute,<br>
* and license this software and its documentation for any purpose, provided<br>
* that existing copyright notices are retained in all copies and that this<br>
* notice is included verbatim in any distributions.  No written agreement,<br>
* license, or royalty fee is required for any of the authorized uses.<br>
* Modifications to this software may be copyrighted by their authors<br>
* and need not follow the licensing terms described here, provided that<br>
* the new terms are clearly indicated on the first page of each file where<br>
* they apply.<br>
* */<br>
EXTERN (__cs3_stack)<br>
EXTERN (__cs3_reset)<br>
EXTERN (__cs3_isr_nmi)<br>
EXTERN (__cs3_isr_hard_fault)<br>
EXTERN (__cs3_isr_mpu_fault)<br>
EXTERN (__cs3_isr_bus_fault)<br>
EXTERN (__cs3_isr_usage_fault)<br>
EXTERN (__cs3_isr_reserved_7)<br>
EXTERN (__cs3_isr_reserved_8)<br>
EXTERN (__cs3_isr_reserved_9)<br>
EXTERN (__cs3_isr_reserved_10)<br>
EXTERN (__cs3_isr_svcall)<br>
EXTERN (__cs3_isr_debug)<br>
EXTERN (__cs3_isr_reserved_13)<br>
EXTERN (__cs3_isr_pendsv)<br>
EXTERN (__cs3_isr_systick)<br>
EXTERN (__cs3_isr_gpio_a)<br>
EXTERN (__cs3_isr_gpio_b)<br>
EXTERN (__cs3_isr_gpio_c)<br>
EXTERN (__cs3_isr_gpio_d)<br>
EXTERN (__cs3_isr_gpio_e)<br>
EXTERN (__cs3_isr_uart0)<br>
EXTERN (__cs3_isr_uart1)<br>
EXTERN (__cs3_isr_ssi0)<br>
EXTERN (__cs3_isr_i2c0)<br>
EXTERN (__cs3_isr_pwm_fault)<br>
EXTERN (__cs3_isr_pwm0)<br>
EXTERN (__cs3_isr_pwm1)<br>
EXTERN (__cs3_isr_pwm2)<br>
EXTERN (__cs3_isr_qei0)<br>
EXTERN (__cs3_isr_adc0)<br>
EXTERN (__cs3_isr_adc1)<br>
EXTERN (__cs3_isr_adc2)<br>
EXTERN (__cs3_isr_adc3)<br>
EXTERN (__cs3_isr_watchdog)<br>
EXTERN (__cs3_isr_timer0a)<br>
EXTERN (__cs3_isr_timer0b)<br>
EXTERN (__cs3_isr_timer1a)<br>
EXTERN (__cs3_isr_timer1b)<br>
EXTERN (__cs3_isr_timer2a)<br>
EXTERN (__cs3_isr_timer2b)<br>
EXTERN (__cs3_isr_comp0)<br>
EXTERN (__cs3_isr_comp1)<br>
EXTERN (__cs3_isr_comp2)<br>
EXTERN (__cs3_isr_sysctl)<br>
EXTERN (__cs3_isr_flashctl)<br>
EXTERN (__cs3_isr_gpio_f)<br>
EXTERN (__cs3_isr_gpio_g)<br>
EXTERN (__cs3_isr_gpio_h)<br>
EXTERN (__cs3_isr_uart2)<br>
EXTERN (__cs3_isr_ssi1)<br>
EXTERN (__cs3_isr_timer3a)<br>
EXTERN (__cs3_isr_timer3b)<br>
EXTERN (__cs3_isr_i2c1)<br>
EXTERN (__cs3_isr_qei1)<br>
EXTERN (__cs3_isr_can0)<br>
EXTERN (__cs3_isr_can1)<br>
EXTERN (__cs3_isr_can2)<br>
EXTERN (__cs3_isr_ethernet0)<br>
EXTERN (__cs3_isr_hibernate)<br>
EXTERN (__cs3_isr_usb0)<br>
EXTERN (__cs3_isr_pwm3)<br>
EXTERN (__cs3_isr_udma)<br>
EXTERN (__cs3_isr_udmaerr)<br>
</pre>