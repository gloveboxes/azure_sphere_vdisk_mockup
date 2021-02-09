#include "comms_manager.h"

static rpa_queue_t* outbound_queue = NULL;
const uint32_t outbound_queue_capacity = 2000;

static MQTT_CONTEXT* mqttContext;
static struct mqtt_client mqttClient;
static bool mqtt_connected = false;
static int previous_channel_id = -1;

static int sockfd = -1;
static bool wolfSslInitialized = false;
//static struct hostent* hent = NULL;

static char* pub_topic_data = NULL;
static char* pub_topic_diag = NULL;
static char* sub_topic_data = NULL;
static char* sub_topic_control = NULL;
static char* sub_topic_paste = NULL;

static char* sub_topic_vdisk_response = NULL;
static char* pub_topic_vdisk_request = NULL;

static uint8_t sendbuf[3072]; /* sendbuf should be large enough to hold multiple whole mqtt messages */
static uint8_t recvbuf[1024 * 6]; /* recvbuf should be large enough any whole mqtt message expected to be received */

static void reconnect_client(struct mqtt_client* client, void** reconnect_state_vptr);
static void send_message(char* message);
static void queue_message(char* message);

static char line_buffer[1024];
static size_t current_char_pos = 0;
static bool line_buffer_dirty = false;
static pthread_mutex_t message_queue_lock;


TOPIC_TYPE topic_type(char* topic_name) {
	if (strcmp(topic_name, sub_topic_data) == 0) {
		return TOPIC_DATA_SUB;
	}
	else if (strcmp(topic_name, sub_topic_control) == 0) {
		return TOPIC_CONTROL_SUB;
	}
	else if (strcmp(topic_name, sub_topic_paste) == 0) {
		return TOPIC_PASTE_SUB;
	}
	else if (strcmp(topic_name, sub_topic_vdisk_response) == 0) {
		return TOPIC_VDISK_SUB;
	}
	return TOPIC_UNKNOWN;
}

bool is_mqtt_connected(void) {
	return mqtt_connected;
}

#pragma GCC push_options
#pragma GCC optimize ("O0")
/**
 * mqtt_daemon send/receive mqtt messages
 */
static void* mqtt_daemon(void* client) {
	struct timespec ts = { 0, 1000 * 1000 * 150 };

	while (1) {
		mqtt_sync(&mqttClient);
		mqtt_connected = mqttClient.error == MQTT_OK;

		if (line_buffer_dirty) {
			queue_message(line_buffer);
		}

		while ((-1 == nanosleep(&ts, &ts)) && (EINTR == errno));
	}
	return NULL;
}
#pragma GCC pop_options


#pragma GCC push_options
#pragma GCC optimize ("O0")
/**
 * dequeue message and pass message to send message function
 */
static void* outbound_mqtt_queue_daemon(void* args) {
	char* data;
	while (true) {
		if (mqttClient.error == MQTT_OK && pub_topic_data != NULL) {
			if (!rpa_queue_pop(outbound_queue, (void**)&data)) {
				printf("pop failed\n");
				return false;
			}

			if (data != NULL) {
				send_message(data);

				free(data);
				data = NULL;
			}
		}
		else {
			nanosleep(&(struct timespec) { 0, 200000000 }, NULL);
		}
	}
	return NULL;
}
#pragma GCC pop_options

static void send_message(char* message) {
	if (mqttClient.error != MQTT_OK || pub_topic_data == NULL || message == NULL) {
		return;
	}

	mqtt_publish(&mqttClient, pub_topic_data, message, strlen(message), MQTT_PUBLISH_QOS_0);
	mqtt_sync(&mqttClient);
	if (mqttClient.error != MQTT_OK) {
		Log_Debug("ERROR: %s\n", mqtt_error_str(mqttClient.error));
	}
}

void request_track(char* message) {
	if (mqttClient.error != MQTT_OK || pub_topic_data == NULL || message == NULL) {
		return;
	}

	mqtt_publish(&mqttClient, pub_topic_vdisk_request, message, strlen(message), MQTT_PUBLISH_QOS_0);
	mqtt_sync(&mqttClient);
	if (mqttClient.error != MQTT_OK) {
		Log_Debug("ERROR: %s\n", mqtt_error_str(mqttClient.error));
	}
}

static void queue_message(char* message) {
	pthread_mutex_lock(&message_queue_lock);

	char* data = (char*)malloc(strlen(message) + 1);
	if (data != NULL) {
		strcpy(data, message);
		if (!rpa_queue_trypush(outbound_queue, data)) {
			Log_Debug("failed to push item onto outbound queue\n");

			if (data != NULL) {
				free(data);
				data = NULL;
			}
		}
	}
	current_char_pos = 0;
	line_buffer_dirty = false;

	//if (consoleFd != -1 && localSerial) {
	//	write(consoleFd, message, strlen(message));
	//	//delay(1);
	//}

	pthread_mutex_unlock(&message_queue_lock);
}

void publish_message(char* message) {
	size_t len = strlen(message);

	if (len + 1 >= sizeof(line_buffer)) // allow for NULL
	{
		if (line_buffer_dirty) {
			queue_message(line_buffer);
		}
		queue_message(message);
		return;
	}

	if (current_char_pos + len + 1 > (sizeof(line_buffer))) // current pos plus NULL
	{
		queue_message(line_buffer);
	}

	pthread_mutex_lock(&message_queue_lock);

	memcpy(line_buffer + current_char_pos, message, len + 1);	// copy NULL too
	line_buffer_dirty = true;
	current_char_pos += len;

	pthread_mutex_unlock(&message_queue_lock);
}

/**
 * init MQTT connection and subscribe desired topics
 */
void init_mqtt(MQTT_CONTEXT* mqttCtx) {
	pthread_mutex_init(&message_queue_lock, NULL);

	mqttContext = mqttCtx;

	mqtt_init_reconnect(&mqttClient,
		reconnect_client, NULL,
		mqttCtx->publish_callback
	);

	if (!rpa_queue_create(&outbound_queue, outbound_queue_capacity)) {
		Log_Debug("failed to create outbound queue\n");
		return;
	}

	lp_startThreadDetached(mqtt_daemon, &mqttClient, "mqtt_daemon");
	lp_startThreadDetached(outbound_mqtt_queue_daemon, NULL, "outbound_mqtt_queue_daemon");
}

// https://man7.org/linux/man-pages/man2/poll.2.html

static bool wait_for_async_connection(int sockfd, int timeout) {
	struct pollfd pfd;

	pfd.fd = sockfd;
	pfd.events = POLLOUT;

	int ret = poll(&pfd, 1, timeout);
	if (ret < 0) {
		Log_Debug("ERROR: poll fail, reason = %d (%s)\n", errno, strerror(errno));
		return false;
	}

	if (!(pfd.revents != 0 && pfd.revents & POLLOUT)) {
		return false;
	}

	int retVal = -1;
	socklen_t retValLen = sizeof(retVal);

	if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &retVal, &retValLen) < 0) {
		Log_Debug("ERROR: getsockopt fail, reason = %d (%s)\n", errno, strerror(errno));
		return false;
	}

	if (retVal != 0) {
		/* socket has a non zero error status */
		Log_Debug("socket error: %s\n", strerror(retVal));
		return false;
	}
	return true;
}

static void reconnect_client(struct mqtt_client* client, void** reconnect_state_vptr) {
	struct addrinfo hints;
	struct addrinfo* answer = NULL;
	mqtt_connected = false;

	if (sockfd != -1) {
		close(sockfd);
		sockfd = -1;
	}

	/* Perform error handling here. */
	if (client->error != MQTT_ERROR_INITIAL_RECONNECT) {
		Log_Debug("reconnect_client: called while client was in error state \"%s\"\n", mqtt_error_str(client->error));
	}

	struct sockaddr_storage addr;
	socklen_t sockaddr_len = sizeof(struct sockaddr_in);


	memset(&addr, 0, sizeof(addr));
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	if (getaddrinfo(mqttContext->server, mqttContext->port, &hints, &answer) < 0 || answer == NULL) {
		Log_Debug("no addr info for responder\n");
		goto cleanupLabel;
	}

	sockaddr_len = answer->ai_addrlen;
	memcpy(&addr, answer->ai_addr, sockaddr_len);
	freeaddrinfo(answer);

	sockfd = socket(addr.ss_family, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if (sockfd < 0) {
		Log_Debug("bad socket fd, out of fds?\n");
		goto cleanupLabel;
	}

	int rt = connect(sockfd, (const struct sockaddr*)&addr, sockaddr_len);

	if (rt != 0 && errno != EINPROGRESS) {
		Log_Debug("Failed to connect socket");
		goto cleanupLabel;
	}

	if (!wait_for_async_connection(sockfd, 10000)) {
		goto cleanupLabel;
	}

	// Connection was made successfully, so allocate wolfSSL session and context.

	/* Reinitialize the client. */
	mqtt_reinit(&mqttClient, sockfd,
		sendbuf, sizeof(sendbuf),
		recvbuf, sizeof(recvbuf)
	);

	/* Create an anonymous session */
	const char* client_id = NULL;
	/* Ensure we have a clean session */
	uint8_t connect_flags = MQTT_CONNECT_CLEAN_SESSION;

	/* Send connection request to the broker. */
	if (mqtt_connect(&mqttClient, client_id, NULL, NULL, 0, NULL, NULL, connect_flags, 30) != MQTT_OK) {
		goto cleanupLabel;
	}

	if (sub_topic_data != NULL && sub_topic_control != NULL && sub_topic_paste != NULL) {
		mqtt_subscribe(&mqttClient, sub_topic_data, 0);
		mqtt_subscribe(&mqttClient, sub_topic_control, 0);
		mqtt_subscribe(&mqttClient, sub_topic_paste, 0);
		mqtt_subscribe(&mqttClient, sub_topic_vdisk_response, 0);
	}

	Log_Debug("mqtt_established\n");
	mqtt_connected = true;
	return;

cleanupLabel:
	if (sockfd != -1) {
		close(sockfd);
		sockfd = -1;
	}
}

bool set_mqtt_subscriptions(int channelId) {
	if (channelId == previous_channel_id) {
		return true;
	}

	mqtt_connected = false;
	previous_channel_id = channelId;

	size_t channel_id_length = 10u; // channel id can range between 0 and 1000000000 - ie max 10 chars

	size_t pub_topic_length = strlen(PUB_TOPIC_DATA);
	pub_topic_length += channel_id_length + 1;  // allow for null

	size_t pub_topic_diag_length = strlen(PUB_TOPIC_DIAG);
	pub_topic_diag_length += channel_id_length + 1;  // allow for null

	size_t sub_topic_length = strlen(SUB_TOPIC_DATA);
	sub_topic_length += channel_id_length + 1;  // allow for null

	size_t sub_topic_control_length = strlen(SUB_TOPIC_CONTROL);
	sub_topic_control_length += channel_id_length + 1;  // allow for null

	size_t sub_topic_paste_length = strlen(SUB_TOPIC_PASTE);
	sub_topic_paste_length += channel_id_length + 1;  // allow for null

	size_t pub_topic_vdisk_request_length = strlen(PUB_TOPIC_VDISK_REQUEST);
	pub_topic_vdisk_request_length += channel_id_length + 1;  // allow for null

	size_t sub_topic_vdisk_response_length = strlen(SUB_TOPIC_VDISK_RESPONSE);
	sub_topic_vdisk_response_length += channel_id_length + 1;  // allow for null

	if (mqttClient.error == MQTT_OK && (sub_topic_data != NULL || sub_topic_control != NULL || sub_topic_paste != NULL || pub_topic_data != NULL || pub_topic_diag != NULL)) {
		mqtt_unsubscribe(&mqttClient, sub_topic_data);
		mqtt_unsubscribe(&mqttClient, sub_topic_control);
		mqtt_unsubscribe(&mqttClient, sub_topic_paste);
		mqtt_unsubscribe(&mqttClient, sub_topic_vdisk_response);

		free(pub_topic_data);
		free(pub_topic_diag);
		free(sub_topic_data);
		free(sub_topic_control);
		free(sub_topic_paste);

		free(sub_topic_vdisk_response);
		free(pub_topic_vdisk_request);

		sub_topic_data = sub_topic_control = pub_topic_data = sub_topic_paste = pub_topic_diag = NULL;
	}

	pub_topic_data = (char*)malloc(pub_topic_length);
	snprintf(pub_topic_data, pub_topic_length, PUB_TOPIC_DATA, channelId);

	pub_topic_diag = (char*)malloc(pub_topic_diag_length);
	snprintf(pub_topic_diag, pub_topic_diag_length, PUB_TOPIC_DIAG, channelId);

	sub_topic_data = (char*)malloc(sub_topic_length);
	snprintf(sub_topic_data, sub_topic_length, SUB_TOPIC_DATA, channelId);

	sub_topic_control = (char*)malloc(sub_topic_control_length);
	snprintf(sub_topic_control, sub_topic_control_length, SUB_TOPIC_CONTROL, channelId);

	sub_topic_paste = (char*)malloc(sub_topic_paste_length);
	snprintf(sub_topic_paste, sub_topic_paste_length, SUB_TOPIC_PASTE, channelId);

	sub_topic_vdisk_response = (char*)malloc(sub_topic_vdisk_response_length);
	snprintf(sub_topic_vdisk_response, sub_topic_vdisk_response_length, SUB_TOPIC_VDISK_RESPONSE, channelId);

	pub_topic_vdisk_request = (char*)malloc(pub_topic_vdisk_request_length);
	snprintf(pub_topic_vdisk_request, pub_topic_vdisk_request_length, PUB_TOPIC_VDISK_REQUEST, channelId);

	if (mqttClient.error == MQTT_OK && sub_topic_data != NULL && sub_topic_control != NULL) {
		mqtt_subscribe(&mqttClient, sub_topic_data, 0);
		mqtt_subscribe(&mqttClient, sub_topic_control, 0);
		mqtt_subscribe(&mqttClient, sub_topic_paste, 0);
		mqtt_subscribe(&mqttClient, sub_topic_vdisk_response, 0);

		Log_Debug("MQTT Connected, Id: %d\n", channelId);
		publish_message("\r\nCONNECTED TO AZURE SPHERE ALTAIR 8800 EMULATOR.\r\n\r\n");
		mqtt_connected = true;
	}
	return mqtt_connected;
}