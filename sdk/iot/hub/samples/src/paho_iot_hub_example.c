// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#ifdef _MSC_VER
// warning C4201: nonstandard extension used: nameless struct/union
#pragma warning(disable : 4201)
#endif
#include <MQTTClient.h>
#ifdef _MSC_VER
#pragma warning(default : 4201)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <az_iot_hub_client.h>
#include <az_result.h>
#include <az_span.h>

// TODO: #564 - Remove the use of the _az_cfh.h header in samples.
//              Note: this is required to work-around MQTTClient.h as well as az_span init issues.
#include <_az_cfg.h>

// Service information
#define HUB_URL "ssl://dawalton-hub.azure-devices.net:8883"
#define HUB_FQDN "dawalton-hub.azure-devices.net"
#define DEVICE_ID "paho_example"

// Device information
#define REGISTRATION_ID_ENV "AZ_IOT_REGISTRATION_ID"

// AZ_IOT_DEVICE_X509_CERT_PEM_FILE is the path to a PEM file containing the device certificate and
// key as well as any intermediate certificates chaining to an uploaded group certificate.
#define DEVICE_X509_CERT_PEM_FILE "AZ_IOT_DEVICE_X509_CERT_PEM_FILE"

// AZ_IOT_DEVICE_X509_TRUST_PEM_FILE is the path to a PEM file containing the server trusted CA
// This is usually not needed on Linux or Mac but needs to be set on Windows.
#define DEVICE_X509_TRUST_PEM_FILE "AZ_IOT_DEVICE_X509_TRUST_PEM_FILE"

static char x509_cert_pem_file[512] = { 0 };
static char x509_trust_pem_file[256] = { 0 };

static az_iot_hub_client client;
static MQTTClient mqtt_client;

static az_result read_configuration_entry(
    const char* name,
    const char* env_name,
    char* default_value,
    bool hide_value,
    az_span buffer,
    az_span* out_value)
{
  printf("%s = ", name);
  char* env = getenv(env_name);

  if (env != NULL)
  {
    printf("%s\n", hide_value ? "***" : env);
    *out_value = az_span_init(az_span_ptr(buffer), 0, az_span_capacity(buffer));
    AZ_RETURN_IF_FAILED(az_span_copy(*out_value, az_span_from_str(env), out_value));
  }
  else if (default_value != NULL)
  {
    printf("%s\n", default_value);
    AZ_RETURN_IF_FAILED(az_span_copy(*out_value, az_span_from_str(default_value), out_value));
  }
  else
  {
    printf("(missing) Please set the %s environment variable.\n", env_name);
    return AZ_ERROR_ARG;
  }

  return AZ_OK;
}

static az_result read_configuration_and_init_client()
{

  az_span cert = AZ_SPAN_LITERAL_FROM_BUFFER(x509_cert_pem_file);
  AZ_RETURN_IF_FAILED(read_configuration_entry(
      "X509 Certificate PEM Store File", DEVICE_X509_CERT_PEM_FILE, NULL, false, cert, &cert));

  az_span trusted = AZ_SPAN_LITERAL_FROM_BUFFER(x509_trust_pem_file);
  AZ_RETURN_IF_FAILED(read_configuration_entry(
      "X509 Trusted PEM Store File", DEVICE_X509_TRUST_PEM_FILE, "", false, trusted, &trusted));

  AZ_RETURN_IF_FAILED(az_iot_hub_client_init(
      &client, AZ_SPAN_FROM_STR(HUB_FQDN), AZ_SPAN_FROM_STR(DEVICE_ID), NULL));

  return AZ_OK;
}

static void print_twin_response_type(az_iot_hub_client_twin_response_type type)
{
  switch (type)
  {
    case AZ_IOT_CLIENT_TWIN_RESPONSE_TYPE_GET:
      printf("A twin GET response was received\r\n");
      break;
    case AZ_IOT_CLIENT_TWIN_RESPONSE_TYPE_DESIRED_PROPERTIES:
      printf("A twin desired properties message was received\r\n");
      break;
    case AZ_IOT_CLIENT_TWIN_RESPONSE_TYPE_REPORTED_PROPERTIES:
      printf("A twin reported properties message was received\r\n");
      break;
  }
}

static int on_received(void* context, char* topicName, int topicLen, MQTTClient_message* message)
{
  UNUSED(context);

  int i;
  char* payloadptr;

  if (topicLen == 0)
  {
    // The length of the topic if there are one more NULL characters embedded in topicName,
    // otherwise topicLen is 0.
    topicLen = (int)strlen(topicName);
  }

  az_result result;

  az_span topic_span = az_span_init((uint8_t*)topicName, topicLen, topicLen);

  az_iot_hub_client_c2d_request c2d_request;
  az_iot_hub_client_twin_response twin_response;
  if (az_iot_hub_client_c2d_received_topic_parse(&client, topic_span, &c2d_request) == AZ_OK)
  {
    printf("C2D Message arrived\n");
    payloadptr = message->payload;
    for (i = 0; i < message->payloadlen; i++)
    {
      putchar(*payloadptr++);
    }
  }
  else if(az_iot_hub_client_twin_received_topic_parse(&client, topic_span, &twin_response) == AZ_OK)
  {
    printf("Twin Message Arrived");
    print_twin_response_type(twin_response.response_type);
    printf("Response status was %i\r\n", twin_response.status);
  }

  (void)result;

  putchar('\n');
  MQTTClient_freeMessage(&message);
  MQTTClient_free(topicName);

  return 1;
}

// int ssl_cb(const char* string, size_t len, void* context);
static int ssl_cb(const char* string, size_t len, void* context)
{
  (void)context;
  (void)len;
  printf("Error: %s", string);
  return 0;
}

static int connect()
{
  int rc;

  MQTTClient_SSLOptions mqtt_ssl_options = MQTTClient_SSLOptions_initializer;
  MQTTClient_connectOptions mqtt_connect_options = MQTTClient_connectOptions_initializer;

  char username[128] = { 0 };
  az_span username_span = AZ_SPAN_LITERAL_FROM_BUFFER(username);
  if ((rc = az_iot_hub_client_user_name_get(&client, username_span, &username_span)) != AZ_OK)

  {
    printf("Failed to get MQTT clientId, return code %d\n", rc);
    return rc;
  }

  if ((rc = az_span_append_uint8(username_span, '\0', &username_span)) != AZ_OK)
  {
    printf("Failed to get MQTT username, return code %d\n", rc);
    return rc;
  }

  mqtt_connect_options.username = (char*)az_span_ptr(username_span);
  mqtt_connect_options.password = NULL;

  mqtt_ssl_options.keyStore = (char*)x509_cert_pem_file;
  if (*x509_trust_pem_file != '\0')
  {
    mqtt_ssl_options.trustStore = (char*)x509_trust_pem_file;
  }

  mqtt_ssl_options.ssl_error_cb = ssl_cb;

  mqtt_connect_options.ssl = &mqtt_ssl_options;

  if ((rc = MQTTClient_connect(mqtt_client, &mqtt_connect_options)) != MQTTCLIENT_SUCCESS)
  {
    printf("Failed to connect, return code %d\n", rc);
    return rc;
  }

  return 0;
}

static int subscribe()
{
  int rc;

  char c2d_topic[128];
  az_span c2d_topic_span = AZ_SPAN_LITERAL_FROM_BUFFER(c2d_topic);
  if ((rc
       = az_iot_hub_client_c2d_subscribe_topic_filter_get(&client, c2d_topic_span, &c2d_topic_span))
      != AZ_OK)

  {
    printf("Failed to get C2D MQTT SUB topic filter, return code %d\n", rc);
    return rc;
  }

  if ((rc = az_span_append_uint8(c2d_topic_span, '\0', &c2d_topic_span)) != AZ_OK)
  {
    printf("Failed to get MQTT SUB topic filter, return code %d\n", rc);
    return rc;
  }

  if ((rc = MQTTClient_subscribe(mqtt_client, c2d_topic, 1)) != MQTTCLIENT_SUCCESS)
  {
    printf("Failed to subscribe, return code %d\n", rc);
    return rc;
  }

  return 0;
}

int main()
{
  int rc;

  if ((rc = read_configuration_and_init_client()) != AZ_OK)
  {
    printf("Failed to read configuration from environment variables, return code %d\n", rc);
    return rc;
  }

  char client_id[128] = { 0 };
  az_span client_id_span = AZ_SPAN_LITERAL_FROM_BUFFER(client_id);
  if ((rc = az_iot_hub_client_id_get(&client, client_id_span, &client_id_span)) != AZ_OK)

  {
    printf("Failed to get MQTT clientId, return code %d\n", rc);
    return rc;
  }

  if ((rc = MQTTClient_create(&mqtt_client, HUB_URL, client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL))
      != MQTTCLIENT_SUCCESS)
  {
    printf("Failed to create MQTT client, return code %d\n", rc);
    return rc;
  }

  if ((rc = MQTTClient_setCallbacks(mqtt_client, NULL, NULL, on_received, NULL))
      != MQTTCLIENT_SUCCESS)
  {
    printf("Failed to set MQTT callbacks, return code %d\n", rc);
    return rc;
  }

  if ((rc = connect()) != 0)
  {
    return rc;
  }

  if ((rc = subscribe()) != 0)
  {
    return rc;
  }

  printf("Subscribed to topics.\n");

  printf("Waiting for activity. [Press ENTER to abort]\n");
  (void)getchar();

  if ((rc = MQTTClient_disconnect(mqtt_client, 10000)) != MQTTCLIENT_SUCCESS)
  {
    printf("Failed to disconnect MQTT client, return code %d\n", rc);
    return rc;
  }

  printf("Disconnected.\n");
  MQTTClient_destroy(&mqtt_client);

  return 0;
}
