#include "nmea_server.h"

#include <stdlib.h>

static nmea_server_config_t nmea_server_default_config(void);

int main(void)
{
    int exit_status = EXIT_SUCCESS;
    const nmea_server_config_t config = nmea_server_default_config();

    if (!nmea_server_config_is_valid(&config))
    {
        exit_status = EXIT_FAILURE;
    }
    else if (nmea_server_run(&config) != NMEA_SERVER_STATUS_SUCCESS)
    {
        exit_status = EXIT_FAILURE;
    }
    else
    {
        /* Nothing else to do. */
    }

    return exit_status;
}

static nmea_server_config_t nmea_server_default_config(void)
{
    nmea_server_config_t config;

    config.latitude_deg = 45.464200;
    config.longitude_deg = 9.190000;
    config.altitude_m = 120.0;
    config.hdop = 0.9;
    config.geoid_separation_m = 47.0;
    config.transmission_period_ms = 10U;
    config.tcp_port = 5000U;
    config.fix_quality = 4U;
    config.satellites_used = 10U;
    config.spurious_bytes_min_length = 1U;
    config.spurious_bytes_max_length = 6U;

    return config;
}
