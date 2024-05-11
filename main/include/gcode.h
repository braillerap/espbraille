#ifndef __GCODE_H
#define __GCODE_H

#include "config.h"

#define GCODE_CMD_SIZE          GCODE_STRING_SIZE
#define GCODE_QUEUE_ELM_NBR     128
#define GCODE_QUEUE_STATUS_NBR  64

typedef enum
{
    PENDING,
    OK,
    ERROR,
    UNKNOWN,
    PING,
    STATUS_NBR
} gcode_status;

typedef struct {
    int32_t         id;
    gcode_status    status;
} gcode_status_cmd;


void        gcode_init (void);
void        gcode_reset (void);
size_t      gcode_get_free_size (void);
const char* gcode_get_status_string (gcode_status status);
uint8_t     gcode_enqueue (char *strcmd, uint32_t cmd_id);
int8_t      get_queued_status (gcode_status_cmd* cmd, uint32_t timeout);


#endif