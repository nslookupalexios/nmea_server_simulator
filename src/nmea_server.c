#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "nmea_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

typedef struct
{
    double absolute_degrees;
    char hemisphere;
    unsigned int degree_width;
} nmea_coordinate_t;

static bool nmea_coordinate_from_decimal(double decimal_degrees,
                                         bool is_latitude,
                                         nmea_coordinate_t *coordinate);
static bool nmea_format_coordinate(const nmea_coordinate_t *coordinate,
                                   char *buffer,
                                   size_t buffer_size);
static uint8_t nmea_compute_checksum(const char *payload);
static bool nmea_get_utc_time(char *buffer, size_t buffer_size);
static int nmea_server_create_listener(uint16_t tcp_port);
static int nmea_server_accept_client(int listener_fd);
static bool nmea_send_all(int socket_fd, const char *buffer, size_t length);

bool nmea_server_config_is_valid(const nmea_server_config_t *config)
{
    bool is_valid = false;

    if (config != NULL)
    {
        is_valid = (config->latitude_deg >= -90.0) &&
                   (config->latitude_deg <= 90.0) &&
                   (config->longitude_deg >= -180.0) &&
                   (config->longitude_deg <= 180.0) &&
                   (config->fix_quality <= 8U) &&
                   (config->satellites_used <= 99U) &&
                   (config->hdop >= 0.0) &&
                   (config->transmission_period_ms > 0U) &&
                   (config->tcp_port > 0U);
    }

    return is_valid;
}

bool nmea_server_build_gga_sentence(const nmea_server_config_t *config,
                                    char *buffer,
                                    size_t buffer_size)
{
    bool is_successful = false;
    char utc_time[7];
    char latitude_buffer[16];
    char longitude_buffer[16];
    char payload[NMEA_SERVER_SENTENCE_MAX_LENGTH];
    nmea_coordinate_t latitude;
    nmea_coordinate_t longitude;
    int payload_length;
    int sentence_length;
    uint8_t checksum;

    if ((config == NULL) || (buffer == NULL) || (buffer_size == 0U))
    {
        return false;
    }

    if (!nmea_server_config_is_valid(config))
    {
        return false;
    }

    if (!nmea_get_utc_time(utc_time, sizeof(utc_time)))
    {
        return false;
    }

    if (!nmea_coordinate_from_decimal(config->latitude_deg, true, &latitude))
    {
        return false;
    }

    if (!nmea_coordinate_from_decimal(config->longitude_deg, false, &longitude))
    {
        return false;
    }

    if (!nmea_format_coordinate(&latitude, latitude_buffer, sizeof(latitude_buffer)))
    {
        return false;
    }

    if (!nmea_format_coordinate(&longitude, longitude_buffer, sizeof(longitude_buffer)))
    {
        return false;
    }

    payload_length = snprintf(payload,
                              sizeof(payload),
                              "GPGGA,%s,%s,%c,%s,%c,%u,%02u,%.1f,%.1f,M,%.1f,M,,",
                              utc_time,
                              latitude_buffer,
                              latitude.hemisphere,
                              longitude_buffer,
                              longitude.hemisphere,
                              (unsigned int)config->fix_quality,
                              (unsigned int)config->satellites_used,
                              config->hdop,
                              config->altitude_m,
                              config->geoid_separation_m);

    if ((payload_length <= 0) || ((size_t)payload_length >= sizeof(payload)))
    {
        return false;
    }

    checksum = nmea_compute_checksum(payload);

    sentence_length = snprintf(buffer,
                               buffer_size,
                               "$%s*%02X\r\n",
                               payload,
                               (unsigned int)checksum);

    if ((sentence_length > 0) && ((size_t)sentence_length < buffer_size))
    {
        is_successful = true;
    }

    return is_successful;
}

int nmea_server_run(const nmea_server_config_t *config)
{
    int status = -1;
    int listener_fd = -1;
    int client_fd = -1;
    char sentence[NMEA_SERVER_SENTENCE_MAX_LENGTH];

    if (!nmea_server_config_is_valid(config))
    {
        return -1;
    }

    listener_fd = nmea_server_create_listener(config->tcp_port);
    if (listener_fd < 0)
    {
        return -1;
    }

    for (;;)
    {
        client_fd = nmea_server_accept_client(listener_fd);
        if (client_fd < 0)
        {
            continue;
        }

        for (;;)
        {
            if (!nmea_server_build_gga_sentence(config, sentence, sizeof(sentence)))
            {
                goto cleanup;
            }

            if (!nmea_send_all(client_fd, sentence, strlen(sentence)))
            {
                break;
            }

            (void)usleep((useconds_t)config->transmission_period_ms * 1000U);
        }

        (void)close(client_fd);
        client_fd = -1;
    }

cleanup:
    if (client_fd >= 0)
    {
        (void)close(client_fd);
    }

    if (listener_fd >= 0)
    {
        (void)close(listener_fd);
    }

    return status;
}

static bool nmea_coordinate_from_decimal(double decimal_degrees,
                                         bool is_latitude,
                                         nmea_coordinate_t *coordinate)
{
    double absolute_value;

    if (coordinate == NULL)
    {
        return false;
    }

    absolute_value = fabs(decimal_degrees);
    coordinate->absolute_degrees = absolute_value;
    coordinate->degree_width = is_latitude ? 2U : 3U;

    if (is_latitude)
    {
        coordinate->hemisphere = (decimal_degrees >= 0.0) ? 'N' : 'S';
    }
    else
    {
        coordinate->hemisphere = (decimal_degrees >= 0.0) ? 'E' : 'W';
    }

    return true;
}

static bool nmea_format_coordinate(const nmea_coordinate_t *coordinate,
                                   char *buffer,
                                   size_t buffer_size)
{
    unsigned int degrees;
    double minutes;
    int length;

    if ((coordinate == NULL) || (buffer == NULL) || (buffer_size == 0U))
    {
        return false;
    }

    degrees = (unsigned int)floor(coordinate->absolute_degrees);
    minutes = (coordinate->absolute_degrees - (double)degrees) * 60.0;

    if (coordinate->degree_width == 2U)
    {
        length = snprintf(buffer, buffer_size, "%02u%07.4f", degrees, minutes);
    }
    else
    {
        length = snprintf(buffer, buffer_size, "%03u%07.4f", degrees, minutes);
    }

    return (length > 0) && ((size_t)length < buffer_size);
}

static uint8_t nmea_compute_checksum(const char *payload)
{
    uint8_t checksum = 0U;
    size_t index;
    size_t length;

    if (payload == NULL)
    {
        return 0U;
    }

    length = strlen(payload);

    for (index = 0U; index < length; ++index)
    {
        checksum ^= (uint8_t)payload[index];
    }

    return checksum;
}

static bool nmea_get_utc_time(char *buffer, size_t buffer_size)
{
    bool is_successful = false;
    time_t current_time;
    struct tm current_tm;

    if ((buffer == NULL) || (buffer_size < 7U))
    {
        return false;
    }

    current_time = time(NULL);
    if (current_time == (time_t)-1)
    {
        return false;
    }

    if (gmtime_r(&current_time, &current_tm) == NULL)
    {
        return false;
    }

    if (strftime(buffer, buffer_size, "%H%M%S", &current_tm) > 0U)
    {
        is_successful = true;
    }

    return is_successful;
}

static int nmea_server_create_listener(uint16_t tcp_port)
{
    int listener_fd;
    int reuse_address = 1;
    struct sockaddr_in server_address;

    listener_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd < 0)
    {
        return -1;
    }

    if (setsockopt(listener_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &reuse_address,
                   (socklen_t)sizeof(reuse_address)) < 0)
    {
        (void)close(listener_fd);
        return -1;
    }

    (void)memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(tcp_port);

    if (bind(listener_fd,
             (const struct sockaddr *)&server_address,
             (socklen_t)sizeof(server_address)) < 0)
    {
        (void)close(listener_fd);
        return -1;
    }

    if (listen(listener_fd, 1) < 0)
    {
        (void)close(listener_fd);
        return -1;
    }

    return listener_fd;
}

static int nmea_server_accept_client(int listener_fd)
{
    int client_fd;
    struct sockaddr_in client_address;
    socklen_t client_length = (socklen_t)sizeof(client_address);

    client_fd = accept(listener_fd,
                       (struct sockaddr *)&client_address,
                       &client_length);

    return client_fd;
}

static bool nmea_send_all(int socket_fd, const char *buffer, size_t length)
{
    size_t total_sent = 0U;

    while (total_sent < length)
    {
        ssize_t current_sent;

        current_sent = send(socket_fd,
                            &buffer[total_sent],
                            length - total_sent,
                            0);

        if (current_sent <= 0)
        {
            if ((current_sent < 0) && (errno == EINTR))
            {
                continue;
            }

            return false;
        }

        total_sent += (size_t)current_sent;
    }

    return true;
}
