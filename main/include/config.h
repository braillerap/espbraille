#ifndef __CONFIG_H
#define __CONFIG_H

#define TXD_PIN (GPIO_NUM_17)
#define RXD_PIN (GPIO_NUM_16)

#define RX_BUF_SIZE (1024*2)
#define TX_BUF_SIZE 0

#define UART UART_NUM_2


#define CONFIG_MDNS_HOST_NAME "wifibrap"
#define MDNS_INSTANCE "braillerap home web server"

extern const char *TAG;

#endif // __CONFIG_H
