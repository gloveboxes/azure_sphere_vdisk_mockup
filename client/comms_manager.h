#pragma once

#include "errno.h"
#include "mqtt.h"
#include "rpa_queue.h"  // https://github.com/chrismerck/rpa_queue

#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/socket.h>
#include <pthread.h>

#include <unistd.h>
#include "utils.h"

#define PUB_TOPIC_DATA		"altair/%d/web"
#define PUB_TOPIC_DIAG		"altair/%d/diag"	// Diagnostics topic
#define SUB_TOPIC_DATA		"altair/%d/dev"
#define SUB_TOPIC_CONTROL	"altair/%d/dev/ctrl"
#define SUB_TOPIC_PASTE		"altair/%d/dev/paste"

#define PUB_TOPIC_VDISK_REQUEST		"altair/%d/vdisk/request"
#define SUB_TOPIC_VDISK_RESPONSE	"altair/%d/vdisk/response"

typedef struct {
	const char* server;
	const char* port;
	const char* mqtt_ca_certificate;
	const char* mqtt_client_private_key;
	const char* mqtt_client_certificate;
	void (*publish_callback)(void** unused, struct mqtt_response_publish* published);
} MQTT_CONTEXT;

typedef enum {
	TOPIC_UNKNOWN,
	TOPIC_DATA_SUB,
	TOPIC_PASTE_SUB,
	TOPIC_CONTROL_SUB,
	TOPIC_VDISK_SUB
} TOPIC_TYPE;


extern int consoleFd;
extern bool localSerial;


bool is_mqtt_connected(void);
void publish_message(char* message);
bool set_mqtt_subscriptions(int channelId);
TOPIC_TYPE topic_type(char* topic_name);
void init_mqtt(MQTT_CONTEXT* mqttCtx);
void request_track(char* message);

