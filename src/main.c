#include "nmea_server.h"

#include <stdio.h>
#include <stdlib.h>

static nmea_server_config_t nmea_server_default_config(void);

int main(void)
{
    const nmea_server_config_t config = nmea_server_default_config();

    if (!nmea_server_config_is_valid(&config))
    {
        (void)fprintf(stderr, "Invalid NMEA server configuration.\n");
        return EXIT_FAILURE;
    }

    (void)printf("Starting NMEA GGA server on TCP port %u at %u ms period.\n",
                 (unsigned int)config.tcp_port,
                 (unsigned int)config.transmission_period_ms);

    if (nmea_server_run(&config) != 0)
    {
        (void)fprintf(stderr, "NMEA server stopped due to an unrecoverable error.\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static nmea_server_config_t nmea_server_default_config(void)
{
    nmea_server_config_t config;

    config.latitude_deg = 45.464200;
    config.longitude_deg = 9.190000;
    config.altitude_m = 120.0;
    config.hdop = 0.9;
    config.geoid_separation_m = 47.0;
    config.transmission_period_ms = 1000U;
    config.tcp_port = 5000U;
    config.fix_quality = 1U;
    config.satellites_used = 10U;

    return config;
}
