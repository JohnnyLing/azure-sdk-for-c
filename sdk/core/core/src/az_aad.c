// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#include "az_aad_private.h"
#include "az_hex_private.h"
#include "az_span_private.h"

#include <az_config_internal.h>
#include <az_http.h>
#include <az_http_internal.h>
#include <az_json.h>
#include <az_platform_internal.h>
#include <az_precondition_internal.h>
#include <az_span_internal.h>

#include <stddef.h>

#include <_az_cfg.h>

AZ_NODISCARD bool _az_token_expired(_az_token const* token)
{
  int64_t const expires_at_msec = token->_internal.expires_at_msec;
  return expires_at_msec <= 0 || az_platform_clock_msec() > expires_at_msec;
}

AZ_NODISCARD az_result _az_token_set(_az_token* self, _az_token const* new_token)
{
  // TODO: thread sync
  *self = *new_token;
  return AZ_OK;
}

AZ_NODISCARD AZ_INLINE bool _az_url_should_encode(uint8_t c)
{
  switch (c)
  {
    case '-':
    case '_':
    case '.':
    case '~':
      return false;
    default:
      return !(('0' <= c && c <= '9') || ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z'));
  }
}

// Does not check ref_destination for NULL.
AZ_NODISCARD az_result _az_url_encode(az_span source, az_span* ref_destination)
{
  int32_t const input_size = az_span_size(source);

  int32_t result_size = 0;
  for (int32_t i = 0; i < input_size; ++i)
  {
    result_size += _az_url_should_encode(az_span_ptr(source)[i]) ? 3 : 1;
  }

  if (az_span_size(*ref_destination) < result_size)
  {
    return AZ_ERROR_INSUFFICIENT_SPAN_SIZE;
  }

  uint8_t* p_s = az_span_ptr(source);
  uint8_t* p_d = az_span_ptr(*ref_destination);
  int32_t s = 0;
  for (int32_t i = 0; i < input_size; ++i)
  {
    uint8_t c = p_s[i];
    if (!_az_url_should_encode(c))
    {
      *p_d = c;
      p_d += 1;
      s += 1;
    }
    else
    {
      p_d[0] = '%';
      p_d[1] = _az_number_to_upper_hex(c >> 4);
      p_d[2] = _az_number_to_upper_hex(c & 0x0F);
      p_d += 3;
      s += 3;
    }
  }

  *ref_destination = az_span_slice_to_end(*ref_destination, result_size);

  return AZ_OK;
}

// https://docs.microsoft.com/en-us/azure/active-directory/develop/v2-oauth2-auth-code-flow#request-an-access-token
AZ_NODISCARD az_result _az_aad_build_url(az_span url, az_span tenant_id, az_span* out_url)
{
  az_span root_url = AZ_SPAN_FROM_STR("https://login.microsoftonline.com/");
  AZ_RETURN_IF_NOT_ENOUGH_SIZE(url, az_span_size(root_url));
  az_span remainder = az_span_copy(url, root_url);

  AZ_RETURN_IF_FAILED(_az_url_encode(tenant_id, &remainder));

  az_span oath_token = AZ_SPAN_FROM_STR("/oauth2/v2.0/token");
  AZ_RETURN_IF_NOT_ENOUGH_SIZE(remainder, az_span_size(oath_token));
  remainder = az_span_copy(remainder, oath_token);

  *out_url = az_span_slice(url, 0, _az_span_diff(remainder, url));

  return AZ_OK;
}

// https://docs.microsoft.com/en-us/azure/active-directory/develop/v2-oauth2-auth-code-flow#request-an-access-token
AZ_NODISCARD az_result _az_aad_build_body(
    az_span body,
    az_span client_id,
    az_span scopes,
    az_span client_secret,
    az_span* out_body)
{
  az_span grant_type_and_client_id_key
      = AZ_SPAN_FROM_STR("grant_type=client_credentials&client_id=");
  AZ_RETURN_IF_NOT_ENOUGH_SIZE(body, az_span_size(grant_type_and_client_id_key));
  az_span remainder = az_span_copy(body, grant_type_and_client_id_key);

  AZ_RETURN_IF_FAILED(_az_url_encode(client_id, &remainder));

  az_span scope_key = AZ_SPAN_FROM_STR("&scope=");
  AZ_RETURN_IF_NOT_ENOUGH_SIZE(remainder, az_span_size(scope_key));
  remainder = az_span_copy(remainder, scope_key);

  AZ_RETURN_IF_FAILED(_az_url_encode(scopes, &remainder));

  if (az_span_size(client_secret) > 0)
  {
    az_span client_secret_key = AZ_SPAN_FROM_STR("&client_secret=");
    AZ_RETURN_IF_NOT_ENOUGH_SIZE(remainder, az_span_size(client_secret_key));
    remainder = az_span_copy(remainder, client_secret_key);

    AZ_RETURN_IF_FAILED(_az_url_encode(client_secret, &remainder));
  }

  *out_body = az_span_slice(body, 0, _az_span_diff(remainder, body));
  return AZ_OK;
}

AZ_NODISCARD az_result _az_aad_request_token(_az_http_request* request, _az_token* out_token)
{
  AZ_RETURN_IF_FAILED(az_http_request_append_header(
      request,
      AZ_SPAN_FROM_STR("Content-Type"),
      AZ_SPAN_FROM_STR("application/x-www-form-urlencoded")));

  uint8_t response_buf[_az_AAD_RESPONSE_BUF_SIZE] = { 0 };
  az_http_response response = { 0 };
  AZ_RETURN_IF_FAILED(az_http_response_init(&response, AZ_SPAN_FROM_BUFFER(response_buf)));

  az_http_policy_retry_options retry_options = _az_http_policy_retry_options_default();
  retry_options.max_retries = 7;
  retry_options.retry_delay_msec = 1 * _az_TIME_MILLISECONDS_PER_SECOND;
  retry_options.max_retry_delay_msec
      = 1 * _az_TIME_SECONDS_PER_MINUTE * _az_TIME_MILLISECONDS_PER_SECOND;

  // Make a HTTP request to get token
  _az_http_pipeline pipeline = (_az_http_pipeline){
    ._internal = {
      .p_policies = {
        {._internal = { .process = az_http_pipeline_policy_retry, .p_options = &retry_options, }, },
        {._internal = { .process = az_http_pipeline_policy_logging, .p_options = NULL, }, },
        {._internal = { .process = az_http_pipeline_policy_transport, .p_options = NULL, }, },
      },
    },
  };

  AZ_RETURN_IF_FAILED(az_http_pipeline_process(&pipeline, request, &response));

  // If we failed to get the token, we return failure/
  az_http_response_status_line status_line = { 0 };
  AZ_RETURN_IF_FAILED(az_http_response_get_status_line(&response, &status_line));
  if (status_line.status_code != AZ_HTTP_STATUS_CODE_OK)
  {
    return AZ_ERROR_HTTP_AUTHENTICATION_FAILED;
  }

  // We successfully got the token, let's parse the body.
  az_span body = { 0 };
  AZ_RETURN_IF_FAILED(az_http_response_get_body(&response, &body));

  // Expiration
  az_json_token json_token;
  AZ_RETURN_IF_FAILED(az_json_parse_by_pointer(body, AZ_SPAN_FROM_STR("/expires_in"), &json_token));

  double expires_in_seconds = 0;
  AZ_RETURN_IF_FAILED(az_json_token_get_number(&json_token, &expires_in_seconds));

  // We'll assume the token expires 3 minutes prior to its actual expiration.
  int64_t const expires_in_msec
      = (((int64_t)expires_in_seconds) - (3 * _az_TIME_SECONDS_PER_MINUTE))
      * _az_TIME_MILLISECONDS_PER_SECOND;

  // Access token
  AZ_RETURN_IF_FAILED(
      az_json_parse_by_pointer(body, AZ_SPAN_FROM_STR("/access_token"), &json_token));

  az_span access_token = { 0 };
  AZ_RETURN_IF_FAILED(az_json_token_get_string(&json_token, &access_token));

  _az_token new_token = {
    ._internal = {
      .token = { 0 },
      .token_length = 0,
      .expires_at_msec = az_platform_clock_msec() + expires_in_msec,
    },
  };

  az_span new_token_span = AZ_SPAN_FROM_BUFFER(new_token._internal.token);
  az_span remainder = az_span_copy(new_token_span, AZ_SPAN_FROM_STR("Bearer "));
  AZ_RETURN_IF_NOT_ENOUGH_SIZE(remainder, az_span_size(access_token));
  remainder = az_span_copy(remainder, access_token);

  new_token._internal.token_length = (int16_t)_az_span_diff(remainder, new_token_span);

  AZ_RETURN_IF_FAILED(_az_token_set(out_token, &new_token));

  return AZ_OK;
}
