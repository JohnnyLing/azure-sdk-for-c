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

static char x509_cert_pem_file[] = "~/device_ec_cert.pem";
static char x509_cert_private_key_file[] = "~/device_ec_key.pem";
static char x509_trust_pem_file[256] = { 0 };

static az_iot_hub_client client;
static MQTTClient mqtt_client;

static az_result init_client()
{
  AZ_RETURN_IF_FAILED(az_iot_hub_client_init(
      &client, AZ_SPAN_FROM_STR(HUB_FQDN), AZ_SPAN_FROM_STR(DEVICE_ID), NULL));

  return AZ_OK;
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

  printf("Message arrived\n");
  printf("     topic (%d): %s\n", topicLen, topicName);
  printf("   message (%d): ", message->payloadlen);

  payloadptr = message->payload;
  for (i = 0; i < message->payloadlen; i++)
  {
    putchar(*payloadptr++);
  }
  
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
  mqtt_ssl_options.privateKey = (char*)x509_cert_private_key_file;
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

// static int subscribe()
// {
//   int rc;

//   char topic_filter[128];
//   az_span topic_filter_span = AZ_SPAN_LITERAL_FROM_BUFFER(topic_filter);
//   if ((rc = az_iot_client_register_subscribe_topic_filter_get(
//            &client, topic_filter_span, &topic_filter_span))
//       != AZ_OK)

//   {
//     printf("Failed to get MQTT SUB topic filter, return code %d\n", rc);
//     return rc;
//   }

//   if ((rc = az_span_append_uint8(topic_filter_span, '\0', &topic_filter_span)) != AZ_OK)
//   {
//     printf("Failed to get MQTT SUB topic filter, return code %d\n", rc);
//     return rc;
//   }

//   if ((rc = MQTTClient_subscribe(mqtt_client, topic_filter, 1)) != MQTTCLIENT_SUCCESS)
//   {
//     printf("Failed to subscribe, return code %d\n", rc);
//     return rc;
//   }

//   return 0;
// }

int main()
{
  int rc;

  if ((rc = init_client()) != AZ_OK)
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

  if ((rc = MQTTClient_create(
           &mqtt_client,
           HUB_URL,
           client_id,
           MQTTCLIENT_PERSISTENCE_NONE,
           NULL))
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

  // if ((rc = subscribe()) != 0)
  // {
  //   return rc;
  // }

  printf("Subscribed.\n");

  printf("Started registration. [Press ENTER to abort]\n");
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
