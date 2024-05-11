#ifndef __CONFIG_H
#define __CONFIG_H

//#define TXD_PIN (GPIO_NUM_1)
//#define RXD_PIN (GPIO_NUM_0)

#define TXD_PIN (GPIO_NUM_17)
#define RXD_PIN (GPIO_NUM_16)

#define RX_BUF_SIZE (1024*2)
#define TX_BUF_SIZE 0

#define UART UART_NUM_2

#define GCODE_TIMEOUT_MS    8000
#define GCODE_STRING_SIZE   128


#define CONFIG_MDNS_HOST_NAME "wifibrap"
#define MDNS_INSTANCE "braillerap home web server"

extern const char *TAG;

#endif // __CONFIG_H
