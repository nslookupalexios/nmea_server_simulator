#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "nmea_server.h"

#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
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

static nmea_server_status_t nmea_coordinate_from_decimal(double decimal_degrees,
                                                         bool is_latitude,
                                                         nmea_coordinate_t *coordinate);
static nmea_server_status_t nmea_format_coordinate(const nmea_coordinate_t *coordinate,
                                                   char *buffer,
                                                   size_t buffer_size);
static uint8_t nmea_compute_checksum(const char *payload);
static nmea_server_status_t nmea_get_utc_time(char *buffer, size_t buffer_size);
static int nmea_server_create_listener(uint16_t tcp_port);
static int nmea_server_accept_client(int listener_fd);
static nmea_server_status_t nmea_send_all(int socket_fd, const char *buffer, size_t length);
static size_t nmea_generate_spurious_non_ascii_bytes(const nmea_server_config_t *config,
                                                     char *buffer,
                                                     size_t buffer_size,
                                                     unsigned int *prng_state);
static nmea_server_status_t nmea_server_build_gga_sentence(const nmea_server_config_t *config,
                                                           char *buffer,
                                                           size_t buffer_size);

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
                   (config->tcp_port > 0U) &&
                   (config->spurious_bytes_min_length >= 1U) &&
                   (config->spurious_bytes_max_length <= NMEA_SERVER_SPURIOUS_BYTES_MAX_LENGTH) &&
                   (config->spurious_bytes_min_length <= config->spurious_bytes_max_length);
    }

    return is_valid;
}

static nmea_server_status_t nmea_server_build_gga_sentence(const nmea_server_config_t *config,
                                                           char *buffer,
                                                           size_t buffer_size)
{
    nmea_server_status_t status = NMEA_SERVER_STATUS_ERROR;
    char utc_time[10];
    char latitude_buffer[16];
    char longitude_buffer[16];
    char payload[NMEA_SERVER_SENTENCE_MAX_LENGTH];
    nmea_coordinate_t latitude;
    nmea_coordinate_t longitude;
    int payload_length;
    int sentence_length;
    uint8_t checksum;

    payload_length = 0;
    sentence_length = 0;
    checksum = 0U;

    if ((config != NULL) && (buffer != NULL) && (buffer_size > 0U))
    {
        if (nmea_server_config_is_valid(config) &&
            (nmea_get_utc_time(utc_time, sizeof(utc_time)) == NMEA_SERVER_STATUS_SUCCESS) &&
            (nmea_coordinate_from_decimal(config->latitude_deg, true, &latitude) ==
             NMEA_SERVER_STATUS_SUCCESS) &&
            (nmea_coordinate_from_decimal(config->longitude_deg, false, &longitude) ==
             NMEA_SERVER_STATUS_SUCCESS) &&
            (nmea_format_coordinate(&latitude, latitude_buffer, sizeof(latitude_buffer)) ==
             NMEA_SERVER_STATUS_SUCCESS) &&
            (nmea_format_coordinate(&longitude, longitude_buffer, sizeof(longitude_buffer)) ==
             NMEA_SERVER_STATUS_SUCCESS))
        {
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

            if ((payload_length > 0) && ((size_t)payload_length < sizeof(payload)))
            {
                checksum = nmea_compute_checksum(payload);

                sentence_length = snprintf(buffer,
                                           buffer_size,
                                           "$%s*%02X\r\n",
                                           payload,
                                           (unsigned int)checksum);

                if ((sentence_length > 0) && ((size_t)sentence_length < buffer_size))
                {
                    status = NMEA_SERVER_STATUS_SUCCESS;
                }
            }
        }
    }

    return status;
}

nmea_server_status_t nmea_server_run(const nmea_server_config_t *config)
{
    nmea_server_status_t status = NMEA_SERVER_STATUS_ERROR;
    int listener_fd = -1;
    int client_fd = -1;
    char sentence[NMEA_SERVER_SENTENCE_MAX_LENGTH];
    char spurious_bytes[NMEA_SERVER_SPURIOUS_BYTES_MAX_LENGTH];
    unsigned int prng_state;

    if (nmea_server_config_is_valid(config))
    {
        bool keep_running = true;

        prng_state = (unsigned int)time(NULL);
        prng_state ^= (unsigned int)config->tcp_port;
        prng_state ^= ((unsigned int)config->transmission_period_ms << 16U);

        listener_fd = nmea_server_create_listener(config->tcp_port);
        if (listener_fd >= 0)
        {
            (void)printf("NMEA server started successfully on TCP port %u with a %u ms period.\n",
                         (unsigned int)config->tcp_port,
                         (unsigned int)config->transmission_period_ms);
            (void)fflush(stdout);
            status = NMEA_SERVER_STATUS_SUCCESS;

            while (keep_running)
            {
                client_fd = nmea_server_accept_client(listener_fd);
                if (client_fd >= 0)
                {
                    bool client_connected = true;

                    while (client_connected)
                    {
                        size_t spurious_length;

                        if (nmea_server_build_gga_sentence(config, sentence, sizeof(sentence)) !=
                            NMEA_SERVER_STATUS_SUCCESS)
                        {
                            status = NMEA_SERVER_STATUS_ERROR;
                            keep_running = false;
                            client_connected = false;
                        }
                        else if (nmea_send_all(client_fd, sentence, strlen(sentence)) !=
                                 NMEA_SERVER_STATUS_SUCCESS)
                        {
                            client_connected = false;
                        }
                        else
                        {
                            spurious_length = nmea_generate_spurious_non_ascii_bytes(config,
                                                                                     spurious_bytes,
                                                                                     sizeof(spurious_bytes),
                                                                                     &prng_state);

                            if (nmea_send_all(client_fd, spurious_bytes, spurious_length) !=
                                NMEA_SERVER_STATUS_SUCCESS)
                            {
                                client_connected = false;
                            }
                            else
                            {
                                (void)usleep((useconds_t)config->transmission_period_ms * 1000U);
                            }
                        }
                    }

                    (void)close(client_fd);
                    client_fd = -1;
                }
            }
        }
    }

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

static nmea_server_status_t nmea_coordinate_from_decimal(double decimal_degrees,
                                                         bool is_latitude,
                                                         nmea_coordinate_t *coordinate)
{
    nmea_server_status_t status = NMEA_SERVER_STATUS_ERROR;
    double absolute_value;

    if (coordinate != NULL)
    {
        absolute_value = fabs(decimal_degrees);
        coordinate->absolute_degrees = absolute_value;

        if (is_latitude)
        {
            coordinate->degree_width = 2U;
            coordinate->hemisphere = (decimal_degrees >= 0.0) ? 'N' : 'S';
        }
        else
        {
            coordinate->degree_width = 3U;
            coordinate->hemisphere = (decimal_degrees >= 0.0) ? 'E' : 'W';
        }

        status = NMEA_SERVER_STATUS_SUCCESS;
    }

    return status;
}

static nmea_server_status_t nmea_format_coordinate(const nmea_coordinate_t *coordinate,
                                                   char *buffer,
                                                   size_t buffer_size)
{
    nmea_server_status_t status = NMEA_SERVER_STATUS_ERROR;
    unsigned int degrees;
    double minutes;
    int length;

    if ((coordinate != NULL) && (buffer != NULL) && (buffer_size > 0U))
    {
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

        if ((length > 0) && ((size_t)length < buffer_size))
        {
            status = NMEA_SERVER_STATUS_SUCCESS;
        }
    }

    return status;
}

static uint8_t nmea_compute_checksum(const char *payload)
{
    uint8_t checksum = 0U;
    size_t index;
    size_t length;

    if (payload != NULL)
    {
        length = strlen(payload);

        for (index = 0U; index < length; ++index)
        {
            checksum ^= (uint8_t)payload[index];
        }
    }

    return checksum;
}

static nmea_server_status_t nmea_get_utc_time(char *buffer, size_t buffer_size)
{
    nmea_server_status_t status = NMEA_SERVER_STATUS_ERROR;
    struct timeval current_time;
    struct tm current_tm;
    unsigned int centiseconds;
    int length;

    if ((buffer != NULL) && (buffer_size >= 10U))
    {
        if (gettimeofday(&current_time, NULL) == 0)
        {
            if (gmtime_r(&current_time.tv_sec, &current_tm) != NULL)
            {
                centiseconds = (unsigned int)(current_time.tv_usec / 10000L);
                length = snprintf(buffer,
                                  buffer_size,
                                  "%02d%02d%02d.%02u",
                                  current_tm.tm_hour,
                                  current_tm.tm_min,
                                  current_tm.tm_sec,
                                  centiseconds);

                if ((length > 0) && ((size_t)length < buffer_size))
                {
                    status = NMEA_SERVER_STATUS_SUCCESS;
                }
            }
        }
    }

    return status;
}

static size_t nmea_generate_spurious_non_ascii_bytes(const nmea_server_config_t *config,
                                                     char *buffer,
                                                     size_t buffer_size,
                                                     unsigned int *prng_state)
{
    size_t index;
    size_t generated_length = 0U;
    unsigned int range;

    if ((config != NULL) && (buffer != NULL) && (prng_state != NULL))
    {
        if (buffer_size >= (size_t)config->spurious_bytes_max_length)
        {
            range = (unsigned int)config->spurious_bytes_max_length -
                    (unsigned int)config->spurious_bytes_min_length + 1U;
            generated_length = (size_t)config->spurious_bytes_min_length +
                               ((size_t)rand_r(prng_state) % (size_t)range);

            for (index = 0U; index < generated_length; ++index)
            {
                const uint8_t random_non_ascii_byte =
                    (uint8_t)(0x80U + ((unsigned int)rand_r(prng_state) % 128U));
                buffer[index] = (char)random_non_ascii_byte;
            }
        }
    }

    return generated_length;
}

static int nmea_server_create_listener(uint16_t tcp_port)
{
    int listener_fd = -1;
    int reuse_address = 1;
    struct sockaddr_in server_address;

    listener_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_fd >= 0)
    {
        if (setsockopt(listener_fd,
                       SOL_SOCKET,
                       SO_REUSEADDR,
                       &reuse_address,
                       (socklen_t)sizeof(reuse_address)) == 0)
        {
            (void)memset(&server_address, 0, sizeof(server_address));
            server_address.sin_family = AF_INET;
            server_address.sin_addr.s_addr = htonl(INADDR_ANY);
            server_address.sin_port = htons(tcp_port);

            if (bind(listener_fd,
                     (const struct sockaddr *)&server_address,
                     (socklen_t)sizeof(server_address)) == 0)
            {
                if (listen(listener_fd, 1) != 0)
                {
                    (void)close(listener_fd);
                    listener_fd = -1;
                }
            }
            else
            {
                (void)close(listener_fd);
                listener_fd = -1;
            }
        }
        else
        {
            (void)close(listener_fd);
            listener_fd = -1;
        }
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

static nmea_server_status_t nmea_send_all(int socket_fd, const char *buffer, size_t length)
{
    size_t total_sent = 0U;
    nmea_server_status_t status = NMEA_SERVER_STATUS_SUCCESS;

    while ((total_sent < length) && (status == NMEA_SERVER_STATUS_SUCCESS))
    {
        ssize_t current_sent;

        current_sent = send(socket_fd,
                            &buffer[total_sent],
                            length - total_sent,
                            MSG_NOSIGNAL);

        if (current_sent <= 0)
        {
            if ((current_sent < 0) && (errno == EINTR))
            {
                /* Retry interrupted transmissions without changing state. */
            }
            else
            {
                status = NMEA_SERVER_STATUS_ERROR;
            }
        }
        else
        {
            total_sent += (size_t)current_sent;
        }
    }

    return status;
}
