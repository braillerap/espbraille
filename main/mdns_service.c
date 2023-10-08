#include <sys/param.h>
#include <mdns.h>
#include <esp_log.h>

#include "config.h"

// mdns init
void initialise_mdns(void)
{
    ESP_LOGI(TAG, "Start mdns service");
    mdns_init();
    mdns_hostname_set(CONFIG_MDNS_HOST_NAME);
    mdns_instance_name_set(MDNS_INSTANCE);

    mdns_txt_item_t serviceTxtData[] = {
        {"board", "espbraille"},
        {"path", "/"}
    };

    ESP_ERROR_CHECK(mdns_service_add("espbraille", "_http", "_tcp", 80, serviceTxtData,
                                     sizeof(serviceTxtData) / sizeof(serviceTxtData[0])));
}

