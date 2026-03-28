#ifndef NMEA_SERVER_H
#define NMEA_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    NMEA_SERVER_SENTENCE_MAX_LENGTH = 128
};

typedef struct
{
    double latitude_deg;
    double longitude_deg;
    double altitude_m;
    double hdop;
    double geoid_separation_m;
    uint16_t transmission_period_ms;
    uint16_t tcp_port;
    uint8_t fix_quality;
    uint8_t satellites_used;
} nmea_server_config_t;

bool nmea_server_config_is_valid(const nmea_server_config_t *config);

bool nmea_server_build_gga_sentence(const nmea_server_config_t *config,
                                    char *buffer,
                                    size_t buffer_size);

int nmea_server_run(const nmea_server_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* NMEA_SERVER_H */
