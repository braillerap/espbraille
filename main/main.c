/* 

*/

#include <sys/param.h>

#include "esp_event.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_mac.h"

#include "nvs_flash.h"

#include "esp_netif.h"
#include "lwip/inet.h"
#include "lwip/err.h"
#include "lwip/sys.h"


#include "esp_http_server.h"

#include "cJSON.h"

#include "driver/uart.h"
#include "driver/gpio.h"

#include "wifi_service.h"
#include "mdns_service.h"
#include "reactdatabase.h"
#include "config.h"
#include "gcode.h"


extern react_database react_datafiles[];
extern react_database_info react_datafiles_info[];

const char *TAG = "espbraille";

static uint32_t _last_activity = 0;


// ws tools
/*
 * async send function, which we put into the httpd work queue
 */

struct async_resp_status {
    httpd_handle_t hd;
    int fd;
    char data[128];
    size_t size;
};

struct async_resp_qsize {
    httpd_handle_t hd;
    int fd;
    size_t size;
};

static int cur_fd = -1;



static void ws_async_send_uart(void *arg)
{
    
    struct async_resp_status *resp_arg = arg;
    
    httpd_handle_t hd = resp_arg->hd;
    int fd = resp_arg->fd;
    httpd_ws_frame_t ws_pkt;
    
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.payload = (uint8_t*)resp_arg->data;
    ws_pkt.len = resp_arg->size;
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    httpd_ws_send_frame_async(hd, fd, &ws_pkt);

    free(resp_arg);
}



static esp_err_t trigger_async_status(httpd_handle_t handle, int fd, uint8_t *data, size_t size)
{
    struct async_resp_status *resp_arg = malloc(sizeof(struct async_resp_status));
    resp_arg->hd = handle;
    resp_arg->fd = fd;
    resp_arg->size = size > sizeof(resp_arg->data) ? sizeof(resp_arg->data) : size;
    memcpy (resp_arg->data, data, resp_arg->size);
    
    return httpd_queue_work(handle, ws_async_send_uart, resp_arg);
}

static int trigger_async_qsize(httpd_handle_t handle, int fd, size_t queuesize)
{
    struct async_resp_status *resp_arg = malloc(sizeof(struct async_resp_status));
    resp_arg->hd = handle;
    resp_arg->fd = fd;
    resp_arg->size = snprintf (resp_arg->data, sizeof(resp_arg->data), "{\"free\":%u}", queuesize);
    
    
    return httpd_queue_work(handle, ws_async_send_uart, resp_arg);
}

// HTTP GET Handler
static esp_err_t react_get_handler(httpd_req_t *req)
{
    for (uint16_t i =0; i < REACT_DATAFILES_INFO_NBR; i++)
    {
        if (strcmp(req->uri, react_datafiles_info[i].path) == 0)
        {
            ESP_LOGI(TAG, "SERVE file %s", req->uri);
            ESP_ERROR_CHECK(httpd_resp_set_hdr (req, "Access-Control-Allow-Origin", "*"));
            httpd_resp_set_type(req, react_datafiles_info[i].mime_type);
            httpd_resp_send(req, (char *) react_datafiles[i].data, *react_datafiles[i].size);
            return ESP_OK;
        }
    }
    return ESP_OK;
}

// root handler(redirect to index.html)
static esp_err_t root_get_handler(httpd_req_t *req)
{
   
    // Set status
    httpd_resp_set_status(req, "302 Temporary Redirect");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/index.html");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "Redirecting to root from %s", req->uri);
    
    return ESP_OK;
}


static esp_err_t handle_ws_req(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        if (cur_fd == -1)
        {
            // start a new connection
            cur_fd = httpd_req_to_sockfd(req);
            ESP_LOGW(TAG, "WS handshake fd=%u", cur_fd);
            _last_activity = xTaskGetTickCount ();
            
            // clear queues
            gcode_reset ();
            return ESP_OK;
        }
        else
        {
            // there is a pending connection 
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Connexion in use");
            return (ESP_ERR_INVALID_STATE);
        }
        
    }

    if (cur_fd == -1)
        return (ESP_ERR_INVALID_STATE);

    uint8_t*            buf = NULL;
    httpd_ws_frame_t    ws_pkt;
    
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;

    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);

    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_ws_recv_frame failed to get frame len with %d", ret);
        return ret;
    }

    if (ws_pkt.len)
    {
        // give cmd to gcode task
        buf = calloc(1, ws_pkt.len + 1);
        if (buf == NULL)
        {
            ESP_LOGE(TAG, "Failed to calloc memory for buf");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "httpd_ws_recv_frame failed with %d", ret);
            free(buf);
            return ret;
        }
        
        //ESP_LOGI(TAG, "Got packet with message:%u %d %s", gcode_get_free_size(), strlen((char*)ws_pkt.payload), ws_pkt.payload);
        _last_activity = xTaskGetTickCount ();
        cJSON *root = cJSON_Parse((char *) ws_pkt.payload);
        if (root != NULL)
        {
            cJSON* gcodecmd = cJSON_GetObjectItem(root, "cmd");
            cJSON* statuscmd = cJSON_GetObjectItem(root, "status");
            cJSON* ctrlcmd = cJSON_GetObjectItem(root, "ctrl");
            cur_fd = httpd_req_to_sockfd(req);        
            if (ctrlcmd != NULL)
            {
                ESP_LOGI(TAG, "CTRL request: '%s'", ctrlcmd->valuestring);
                if (strcmp(ctrlcmd->valuestring, "end") == 0)
                {
                    //httpd_req_async_handler_complete (req);
                    ESP_LOGI(TAG, "CLOSE request");
                    cur_fd = -1;
                    gcode_close ();
                }
            }
            else if (gcodecmd != NULL)
            {
                int id = cJSON_GetObjectItem(root, "id")->valueint;

                //ESP_LOGI(TAG, "GCODE request: '%s'", gcodecmd->valuestring);
                gcode_enqueue (gcodecmd->valuestring, id);

                // send queue size
                size_t qlen = gcode_get_free_size();
                
                int ret = trigger_async_qsize (req->handle, httpd_req_to_sockfd(req), qlen);
                //ESP_LOGI(TAG, "Sending status: free size=%u fd=%d ret=%d", qlen, cur_fd, ret);
            }
            else if (statuscmd != NULL)
            {
                ESP_LOGI(TAG, "STATUS request: '%s'", statuscmd->valuestring);
                // send queue size
                int ret = trigger_async_qsize (
                    req->handle, 
                    httpd_req_to_sockfd(req), 
                    gcode_get_free_size());    
                ESP_LOGI(TAG, "Sending status: fd=%d ret=%d", cur_fd, ret);
            }
            else
            {
                /* 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "CMD null value");
                ESP_LOGI(TAG, "Sending ERR500: fd=%d", cur_fd);
            }
            cJSON_Delete(root);
        }
    }

    free (buf);
    return ESP_OK;
}

static const httpd_uri_t root = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = root_get_handler
};

static const httpd_uri_t ws = {
        .uri = "/ws",
        .method = HTTP_GET,
        .handler = handle_ws_req,
        .user_ctx = NULL,
        .is_websocket = true
};

static httpd_uri_t uri_data[REACT_DATAFILES_INFO_NBR];

// HTTP Error (404) Handler - Redirects all requests to the root page
esp_err_t http_404_error_handler(httpd_req_t *req, httpd_err_code_t err)
{
    if (req->method == HTTP_OPTIONS)
    {
        //ESP_LOGI(TAG, "Options request:%s", req->uri);    
        ESP_ERROR_CHECK(httpd_resp_set_hdr (req, "Access-Control-Allow-Origin", "*"));
        ESP_ERROR_CHECK(httpd_resp_set_hdr (req, "Access-Control-Allow-Methods", "GET, POST"));
        ESP_ERROR_CHECK(httpd_resp_set_hdr (req, "Access-Control-Allow-Headers", "Content-Type, origin"));
        httpd_resp_sendstr(req, "OPTIONS answer");
        return ESP_OK;
    }
    
    // Set status
    httpd_resp_set_status(req, "302 Temporary Redirect");
    // Redirect to the "/" root directory
    httpd_resp_set_hdr(req, "Location", "/index.html");
    // iOS requires content in the response to detect a captive portal, simply redirecting is not sufficient.
    httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

    ESP_LOGI(TAG, "404 Redirecting to root from:%s", req->uri);
    
    return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 13;
    config.lru_purge_enable = true;
    config.max_uri_handlers  = 32;

    for (uint16_t i =0; i < REACT_DATAFILES_INFO_NBR; i++)
    {
        uri_data[i].uri = react_datafiles_info[i].path;
        uri_data[i].method = HTTP_GET;
        uri_data[i].handler = react_get_handler;
    }

    // Start the httpd server
    ESP_LOGI(TAG, "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        // Set URI handlers
        ESP_LOGI(TAG, "Registering URI handlers");
        for (uint16_t i =0; i < REACT_DATAFILES_INFO_NBR; i++)
        {
            if (httpd_register_uri_handler(server, &uri_data[i]) != ESP_OK)
                ESP_LOGI(TAG, "Fail to register uri :%s", uri_data[i].uri);
        }

        httpd_register_uri_handler(server, &root);
        httpd_register_uri_handler(server, &ws);
        
        httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, http_404_error_handler);
    }
    return server;
}

void app_main(void)
{
    
    httpd_handle_t server;
    /*
        Turn of warnings from HTTP server as redirecting traffic will yield
        lots of invalid requests
    */
    esp_log_level_set("httpd_uri", ESP_LOG_ERROR);
    esp_log_level_set("httpd_txrx", ESP_LOG_ERROR);
    esp_log_level_set("httpd_parse", ESP_LOG_ERROR);

    

    // Initialize NVS needed by Wi-Fi
    //ESP_ERROR_CHECK(nvs_flash_init());
    
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    
    start_wifi ();

    
    // start gcode processing queue
    gcode_init ();

    // Start the server for the first time
    ESP_LOGI(TAG, "Start HTTP server");
    server = start_webserver();

    initialise_mdns();
    

    _last_activity = xTaskGetTickCount();
    while (server != NULL)
    {
        if (cur_fd != -1)
        {
            gcode_status_cmd status;
            //ESP_LOGI(TAG, "Waiting for gcode status");
            if (get_queued_status (&status, pdMS_TO_TICKS(500)) != 0)
            {
                static char buf[128];
                int size = snprintf (buf, sizeof(buf), 
                    "{\"free\":%u,\"id\":%lu,\"status\":\"%s\"}",
                    gcode_get_free_size(),
                    status.id,
                    gcode_get_status_string(status.status));   
                
                ESP_LOGI(TAG, "Sending status:%s", buf);

                if (size > 0)      
                    trigger_async_status (server, cur_fd, (uint8_t *)buf,  size);
                else
                {
                    ESP_LOGE(TAG, "Error sending status");
                    //cur_fd = -1;
                }
                if (status.status == ERROR)
                {
                    ESP_LOGE(TAG, "Reset socket because Status ERROR");
                    gcode_reset();
                    cur_fd = -1;
                }
                _last_activity = xTaskGetTickCount ();
            }

            if (xTaskGetTickCount () - _last_activity > pdMS_TO_TICKS(GCODE_TIMEOUT_MS) 
                && cur_fd != -1)
            {
                ESP_LOGE(TAG, "Activity TIMEOUT - Reset socket");
                gcode_reset ();
                cur_fd = -1;
            }
            

        }
        else
            vTaskDelay(pdMS_TO_TICKS(25));
        
    }
    
}
