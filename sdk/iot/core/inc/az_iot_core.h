// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/**
 * @file az_iot_core.h
 *
 * @brief Azure IoT common definitions.
 */

#ifndef _az_IOT_CORE_H
#define _az_IOT_CORE_H

#include <az_result.h>
#include <az_span.h>

#include <stdbool.h>
#include <stdint.h>

#include <_az_cfg_prefix.h>

/**
 * @brief Azure IoT service MQTT connection properties.
 *
 */
enum
{
  AZ_CLIENT_DEFAULT_MQTT_CONNECT_PORT = 8883,
  AZ_CLIENT_DEFAULT_MQTT_CONNECT_CLEAN_SESSION = 0x20,
  AZ_CLIENT_DEFAULT_MQTT_CONNECT_KEEPALIVE_SECONDS = 240
};

/**
 * @brief Azure IoT service status codes.
 *
 */
typedef enum
{
  // Service success codes
  AZ_IOT_STATUS_OK = 200,
  AZ_IOT_STATUS_ACCEPTED = 202,
  AZ_IOT_STATUS_NO_CONTENT = 204,

  // Service error codes
  AZ_IOT_STATUS_BAD_REQUEST = 400,
  AZ_IOT_STATUS_UNAUTHORIZED = 401,
  AZ_IOT_STATUS_FORBIDDEN = 403,
  AZ_IOT_STATUS_NOT_FOUND = 404,
  AZ_IOT_STATUS_NOT_ALLOWED = 405,
  AZ_IOT_STATUS_NOT_CONFLICT = 409,
  AZ_IOT_STATUS_PRECONDITION_FAILED = 412,
  AZ_IOT_STATUS_REQUEST_TOO_LARGE = 413,
  AZ_IOT_STATUS_UNSUPPORTED_TYPE = 415,
  AZ_IOT_STATUS_THROTTLED = 429,
  AZ_IOT_STATUS_CLIENT_CLOSED = 499,
  AZ_IOT_STATUS_SERVER_ERROR = 500,
  AZ_IOT_STATUS_BAD_GATEWAY = 502,
  AZ_IOT_STATUS_SERVICE_UNAVAILABLE = 503,
  AZ_IOT_STATUS_TIMEOUT = 504,
} az_iot_status;

/**
 * @brief Get the #az_iot_status from an int.
 *
 * @param[in] status_int The int with the status number.
 * @param[out] status The #az_iot_status* with the status enum.
 * @return The #az_result with the result of the get operation.
 *  @retval #AZ_OK If the int is an #az_iot_status enum. `status` will be set to the according
 * enum.
 *  @retval #AZ_ERROR_ITEM_NOT_FOUND If the int is NOT an #az_iot_status enum.
 */

AZ_NODISCARD az_result az_iot_get_status_from_uint32(uint32_t status_int, az_iot_status* status);

/**
 * @brief Checks if the status indicates a successful operation.
 *
 * @param[in] status The #az_iot_status to verify.
 * @return True if the status indicates success. False otherwise.
 */
AZ_NODISCARD bool az_iot_is_success_status(az_iot_status status);

/**
 * @brief Checks if the status indicates a retriable error occurred during the
 *        operation.
 *
 * @param[in] status The #az_iot_status to verify.
 * @return True if the operation should be retried. False otherwise.
 */
AZ_NODISCARD bool az_iot_is_retriable_status(az_iot_status status);

/**
 * @brief Calculates the recommended delay before retrying an operation that failed.
 *
 * @param[in] operation_msec The time it took, in milliseconds, to perform the operation that
 *                           failed.
 * @param[in] attempt The number of failed retry attempts.
 * @param[in] retry_delay_msec The minimum time, in milliseconds, to wait before a retry.
 * @param[in] max_retry_delay_msec The maximum time, in milliseconds, to wait before a retry.
 * @param[in] random_msec A random value between 0 and the maximum allowed jitter, in milliseconds.
 * @return The recommended delay in milliseconds.
 */
AZ_NODISCARD int32_t az_iot_retry_calc_delay(
    int32_t operation_msec,
    int16_t attempt,
    int32_t retry_delay_msec,
    int32_t max_retry_delay_msec,
    int32_t random_msec);

/**
 * @brief az_span_token is a string tokenizer for az_span.
 *
 * @param[in] source The #az_span with the content to be searched on. It must be a non-empty
 * #az_span.
 * @param[in] delimiter The #az_span containing the delimiter to "split" `source` into tokens.  It
 * must be a non-empty #az_span.
 * @param[out] out_remainder The #az_span pointing to the remaining bytes in `source`, starting
 * after the occurrence of `delimiter`. If the position after `delimiter` is the end of `source`,
 * `out_remainder` is set to an empty #az_span.
 * @return The #az_span pointing to the token delimited by the beginning of `source` up to the first
 * occurrence of (but not including the) `delimiter`, or the end of `source` if `delimiter` is not
 * found. If `source` is empty, AZ_SPAN_NULL is returned instead.
 */
AZ_NODISCARD az_span az_span_token(az_span source, az_span delimiter, az_span* out_remainder);

/**
 * @brief _az_iot_u32toa_size gives the length, in bytes, of the string that would represent the given number.
 *
 * @param[in] number The number whose length, as a string, is to be evaluated.
 * @return The length (not considering null terminator) of the string that would represent the given
 * number.
 */
AZ_NODISCARD int32_t _az_iot_u32toa_size(uint32_t number);

#include <_az_cfg_suffix.h>

#endif //!_az_IOT_CORE_H
