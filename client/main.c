#include "comms_manager.h"
#include <pthread.h>


// Uses self signed CA Certificate
#define MQTT_BROKER_URL "test.mosquitto.org"
#define MQTT_BROKER_PORT "1883"

static void publish_callback(void** unused, struct mqtt_response_publish* published);

static MQTT_CONTEXT mqtt_context = {
    .server = MQTT_BROKER_URL,
    .port = MQTT_BROKER_PORT,
    .publish_callback = publish_callback
};

pthread_cond_t cond1 = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

char diskTrack[1024] = { 0 };
bool diskTrackDirty = false;

/**
 * Process messages received on subscribed topics
 */
static void publish_callback(void** unused, struct mqtt_response_publish* published) {
    bool data_queued = false;
    char command[20];
    char* topic_name = (char*)malloc(published->topic_name_size + 1u);
    memcpy(topic_name, published->topic_name, published->topic_name_size);
    topic_name[published->topic_name_size] = '\0';

    // cap buffered message length to 256 chars - this is max for mbasic
    published->application_message_size = published->application_message_size > 256 ? 256 : published->application_message_size;

    char* application_message = (char*)malloc(published->application_message_size + 2u);
    memcpy(application_message, published->application_message, published->application_message_size);
    application_message[published->application_message_size] = 0x00;
    application_message[published->application_message_size + 1] = 0x00;

    TOPIC_TYPE topic = topic_type(topic_name);

    switch (topic) {
    case TOPIC_VDISK_SUB:	// data message
        pthread_mutex_lock(&lock);

        strcpy(diskTrack, application_message);
        diskTrackDirty = true;

        pthread_cond_signal(&cond1);
        pthread_mutex_unlock(&lock);

        break;
    default:
        break;
    }

    free(topic_name);
    topic_name = NULL;

    free(application_message);
    application_message = NULL;
}

char *vdisk_request(char* requestTrack) {
    char *data = NULL;

    request_track(requestTrack);

    struct timespec now = { 0,0 };
	clock_gettime(CLOCK_REALTIME, &now);
    now.tv_sec += 5;

    pthread_mutex_lock(&lock);
    pthread_cond_timedwait(&cond1, &lock, &now);     

    if (diskTrackDirty) {
        diskTrackDirty = false;
        data = diskTrack;
    }    

    pthread_mutex_unlock(&lock);   

    return data;
}

int main() {
    char *data = NULL;
    char requestTrack[100];

    Log_Debug_Time_Init(2048);

    init_mqtt(&mqtt_context);
    set_mqtt_subscriptions(766786);

    for (size_t i = 0; i < 1000; i++)
    {
        snprintf(requestTrack, sizeof(requestTrack), "Request track %d", i);
        data = vdisk_request(requestTrack);
        if (data != NULL){
            Log_Debug("%s\n", data);
        }
        else {
            Log_Debug("Track read failed\n");
        }
    }

    pause();

}
