#include <sys/param.h>
#include <string.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_log.h>

#include "driver/uart.h"
#include "driver/gpio.h"


#include "gcode.h"
#include "config.h"


#define GCODE_TIMEOUT_READ    250
#define GCODE_TIMEOUT_WRITE    25
#define PENDING_STATUS_DELAY    500

const char* TAGE="espbraille.gcode";

typedef struct {
    char            cmd[GCODE_CMD_SIZE];
    uint32_t        id;
    gcode_status    status;
} gcode_cmd;


static uint8_t gcode_queue_buf[sizeof(gcode_cmd) * GCODE_QUEUE_ELM_NBR];
static QueueHandle_t gcode_queue;
static StaticQueue_t _gcode_queue_storage;

static uint8_t gcode_queue_status_buf[sizeof(gcode_status_cmd) * GCODE_QUEUE_STATUS_NBR];
static QueueHandle_t gcode_queue_status;
static StaticQueue_t _gcode_queue_status_storage;

static SemaphoreHandle_t gcode_mutex = NULL;
static StaticSemaphore_t _mutex_storage;

// gcode buffer
static uint8_t gcodedata[GCODE_CMD_SIZE];
static int16_t gcode_used = 0;
static int32_t gcode_last_cmd_id = -1;
static gcode_status gcode_last_status = CLOSE;

static int8_t set_status (int32_t cmd_id, gcode_status status)
{
    if( xSemaphoreTake( gcode_mutex, ( TickType_t ) 50 ) == pdTRUE )
    {
        if (cmd_id != -1)
        {
            gcode_last_cmd_id = cmd_id;
            gcode_last_status = status;
        }
        xSemaphoreGive( gcode_mutex );
    }
    else
    {
        return (-1);
    }
    return (0);
}

static int8_t signal_status (uint32_t cmd_id, gcode_status status)
{
    gcode_status_cmd cmd;
    
    cmd.status = status;
    cmd.id = cmd_id;

    if (xQueueSend (gcode_queue_status, (void *) &cmd, ( TickType_t ) 50 ) != pdPASS)
        return -1;
    
    return set_status (cmd_id, status);
    
    
}

static gcode_status    send_gcode (gcode_cmd* pcmd)
{
    uint32_t lastevent = xTaskGetTickCount ();
    uint32_t last_pending = 0;
    
    // write cmd on uart
    set_status (pcmd->id, PENDING);
    ESP_LOGI(TAGE, "Write GCODE on UART%d:%s", UART, pcmd->cmd);
    
    // clear RX fifo
    uart_flush(UART);

    // tx cmd
    uart_write_bytes(UART, pcmd->cmd, strlen(pcmd->cmd));
    ESP_ERROR_CHECK(uart_wait_tx_done(UART, GCODE_TIMEOUT_WRITE));
    
    // wait for answer
    lastevent = xTaskGetTickCount ();
    memset (gcodedata, 0, sizeof(gcodedata));
    gcode_used = 0;
    while (true)
    {
        int length = 0;
        ESP_ERROR_CHECK(uart_get_buffered_data_len(UART, (size_t*)&length));
        if (gcode_last_status == CLOSE)
        {
            if (length > 0)
                length = uart_read_bytes(UART, &gcodedata[gcode_used], 
                    length > sizeof(gcodedata) - gcode_used ? sizeof(gcodedata) - gcode_used : length, 
                    pdMS_TO_TICKS(GCODE_TIMEOUT_READ));
            gcode_used = 0;
            gcodedata[gcode_used] = '\0';
            break;
        }
        else if (length > 0)
        {
            ESP_LOGI(TAGE, "byte received %d %d", length, gcode_used);
            length = uart_read_bytes(UART, &gcodedata[gcode_used], 
                length > sizeof(gcodedata) - gcode_used ? sizeof(gcodedata) - gcode_used : length, 
                pdMS_TO_TICKS(GCODE_TIMEOUT_READ));
            gcode_used += length;
            gcodedata[gcode_used] = '\0';
           
            lastevent = xTaskGetTickCount ();
        }

        if (gcode_used > 0)
        {
            char* find = strchr ((char *) gcodedata, '\r');
            if (find == NULL)
                find = strchr ((char *) gcodedata, '\n');
            uint16_t pos = 0;
            
            if (find != NULL)
                pos = find - (char*) gcodedata;
            
            ESP_LOGI(TAGE, "buf %s", (char *) gcodedata);
            ESP_LOGI(TAGE, "pos %d %x", pos, (unsigned int) find);
            if (pos < sizeof(gcodedata))
            {
                gcodedata[pos] = '\0';
                if (strstr((char *)gcodedata, "error") != NULL)
                {
                    // signal error
                    signal_status (pcmd->id, ERROR);
                    ESP_LOGI(TAGE, "sending status ERROR %d %s", (int) pcmd->id, pcmd->cmd);
                    memmove (gcodedata, &gcodedata[pos + 1], gcode_used - pos - 1);
                    gcode_used -= (pos +1 );
                    break;
                }
                else if (strstr ((char *)gcodedata, "ok") != NULL)
                {
                    // signal ok
                    signal_status (pcmd->id, OK);
                    ESP_LOGI(TAGE, "sending status OK %d %s", (int) pcmd->id, pcmd->cmd);
                    memmove (gcodedata, &gcodedata[pos + 1], gcode_used - pos - 1);
                    gcode_used -= (pos +1 );
                    break;
                }
                else
                {
                    if (xTaskGetTickCount () - last_pending > pdMS_TO_TICKS(PENDING_STATUS_DELAY))
                    {
                        signal_status (pcmd->id, PENDING);
                        ESP_LOGI(TAGE, "sending status PENDING %d %s", (int) pcmd->id, pcmd->cmd);
                        last_pending = xTaskGetTickCount ();
                    }
                    memmove (gcodedata, &gcodedata[pos + 1], gcode_used - pos - 1);
                    gcode_used -= (pos +1 );
                }
            }
            else
            {
                memset(gcodedata,0, sizeof(gcodedata));
                gcode_used = 0;
                signal_status (pcmd->id, ERROR);
                break;
            }
        }

        if (xTaskGetTickCount () - lastevent > pdMS_TO_TICKS(GCODE_TIMEOUT_MS))
        {
            ESP_LOGI(TAGE, "Signal TIMEOUT ERROR %lu ms elapsed", ((xTaskGetTickCount () - lastevent) * configTICK_RATE_HZ) / 1000L );
            
            xQueueReset (gcode_queue);
            memset(gcodedata,0, sizeof(gcodedata));
            gcode_used = 0;
            signal_status (pcmd->id, ERROR);
            break;
        }
        else
            vTaskDelay (pdMS_TO_TICKS(1));
    }
    return gcode_last_status;
}

void gcode_reset (void)
{
    // reset gcode queue
    set_status (-1, PENDING);
    xQueueReset (gcode_queue);
    xQueueReset (gcode_queue_status);
}
void gcode_close (void)
{
    // reset gcode queue
    set_status (-1, CLOSE);
    xQueueReset (gcode_queue);
    xQueueReset (gcode_queue_status);

}


uint32_t get_last_cmd (void)
{
    uint32_t last = (uint32_t) 0xFFFFFFFF;
    if( xSemaphoreTake (gcode_mutex, ( TickType_t ) 100 ) == pdTRUE)
    {
        last =  gcode_last_cmd_id;
        
        xSemaphoreGive (gcode_mutex);
    }

    return (last);
}

gcode_status get_last_status (void)
{
    gcode_status status = UNKNOWN;
    if( xSemaphoreTake (gcode_mutex, ( TickType_t ) 100 ) == pdTRUE)
    {
        status =  gcode_last_status;
        
        xSemaphoreGive (gcode_mutex);
    }

    return (status);
}

static const char* status_str[] = {
    "PENDING",
    "OK",
    "ERROR",
    "UNKNOWN",
    "CLOSE"
};

const char *gcode_get_status_string (gcode_status status)
{
    if (status < STATUS_NBR)
        return (status_str[(int)status]);
    else
        return (status_str[UNKNOWN]);
}

int8_t get_queued_status (gcode_status_cmd* cmd, uint32_t timeout)
{
    if ( xQueueReceive(
                        gcode_queue_status,
                        cmd,
                        pdMS_TO_TICKS(timeout)
                    ) == pdPASS)
        {
            return (1);
        }
    return (0);
}

uint8_t gcode_enqueue (char *strcmd, uint32_t cmd_id)
{
    gcode_cmd cmd;
    strncpy (cmd.cmd, strcmd, GCODE_CMD_SIZE);
    cmd.status = PENDING;
    cmd.id = cmd_id;

    if (xQueueSend (gcode_queue, (void *) &cmd, ( TickType_t ) 250 ) != pdPASS)
        return 0;
    return 1;
}

size_t gcode_get_free_size (void)
{
    size_t nbel = (size_t) uxQueueMessagesWaiting (gcode_queue);
    
    return (GCODE_QUEUE_ELM_NBR > nbel ? GCODE_QUEUE_ELM_NBR - nbel : 0);
}



void gcode_task(void *pvParameters)
{
    static gcode_cmd cmd;
    (void) pvParameters;

    #if 1
    // init serial port
     const uart_config_t uart_config = {
        .baud_rate = 250000,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB
        
    };
    memset (gcodedata, 0, sizeof(gcodedata));
    // We won't use a buffer for sending data.
    ESP_ERROR_CHECK(uart_driver_install(UART, RX_BUF_SIZE, TX_BUF_SIZE, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_set_mode(UART, UART_MODE_UART));
    #endif

    while (true) {
        if ( xQueueReceive(
                               gcode_queue,
                               &cmd,
                               pdMS_TO_TICKS(250)
                            ) == pdPASS)
        {
            send_gcode (&cmd);
        }
        
    }
}
void gcode_init (void)
{
  
    gcode_queue = xQueueCreateStatic (GCODE_QUEUE_ELM_NBR, sizeof(gcode_cmd),
        gcode_queue_buf,
        &_gcode_queue_storage
        );

    gcode_queue_status = xQueueCreateStatic (GCODE_QUEUE_STATUS_NBR, sizeof(gcode_status_cmd),
        gcode_queue_status_buf,
        &_gcode_queue_status_storage
        );

    gcode_mutex = xSemaphoreCreateMutexStatic( &_mutex_storage );

    gcode_reset ();

    xTaskCreate(gcode_task, "gcode", 4096, NULL, 1, NULL);
}