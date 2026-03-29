#ifndef NMEA_SERVER_H
#define NMEA_SERVER_H

#define NMEA_SERVER_SENTENCE_MAX_LENGTH (128U)
#define NMEA_SERVER_SPURIOUS_BYTES_MAX_LENGTH (8U)

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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
    uint8_t spurious_bytes_min_length;
    uint8_t spurious_bytes_max_length;
} nmea_server_config_t;

typedef uint8_t nmea_server_status_t;

#define NMEA_SERVER_STATUS_SUCCESS ((nmea_server_status_t)0U)
#define NMEA_SERVER_STATUS_ERROR   ((nmea_server_status_t)1U)

bool nmea_server_config_is_valid(const nmea_server_config_t *config);

nmea_server_status_t nmea_server_run(const nmea_server_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* NMEA_SERVER_H */
