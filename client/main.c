#include "comms_manager.h"
#include <pthread.h>


// Uses self signed CA Certificate
#define MQTT_BROKER_URL "mosquitto.australiaeast.cloudapp.azure.com"
#define MQTT_BROKER_PORT "1883"

static void publish_callback(void** unused, struct mqtt_response_publish* published);

static MQTT_CONTEXT mqtt_context = {
    .server = MQTT_BROKER_URL,
    .port = MQTT_BROKER_PORT,
    .publish_callback = publish_callback
};

pthread_cond_t cond1 = PTHREAD_COND_INITIALIZER;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

char diskTrack[(1024 * 4) + 1] = { 0 };
bool diskTrackDirty = false;

int64_t requestTime;
int64_t responseTime;

/**
 * Process messages received on subscribed topics
 */
static void publish_callback(void** unused, struct mqtt_response_publish* published) {
    bool data_queued = false;
    char command[20];
    char* topic_name = (char*)malloc(published->topic_name_size + 1u);
    memcpy(topic_name, published->topic_name, published->topic_name_size);
    topic_name[published->topic_name_size] = '\0';

    TOPIC_TYPE topic = topic_type(topic_name);

    switch (topic) {
    case TOPIC_VDISK_SUB:	// vdisk response message

        // This is the callback for the vdisk response message

        pthread_mutex_lock(&lock);

        size_t len = published->application_message_size > 4 * 1024 ? 4 * 1024 : published->application_message_size;

        // Log_Debug("Recieved %d bytes", len );
        memcpy(diskTrack, published->application_message, len);
        diskTrack[len] = 0x00;

        diskTrackDirty = true;

        pthread_cond_signal(&cond1);
        pthread_mutex_unlock(&lock);

        break;
    default:
        break;
    }

    free(topic_name);
    topic_name = NULL;

}

char* vdisk_request(char* requestTrack) {
    char* data = NULL;

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
    char* data = NULL;
    char requestTrack[100];

    Log_Debug_Time_Init(2048);

    init_mqtt(&mqtt_context);
    set_mqtt_subscriptions(766786);

    for (size_t i = 0; i < 1000; i++)
    {
        snprintf(requestTrack, sizeof(requestTrack), "Request track %d", i);

        requestTime = lp_getNowMilliseconds();
        data = vdisk_request(requestTrack);
        responseTime = lp_getNowMilliseconds();
        uint64_t total_ms = responseTime - requestTime;

        Log_Debug("Response time in milliseconds %lu\n", total_ms );

        if (data != NULL) {
            Log_Debug("%d Bytes recieved\n", strlen(data));
        }
        else {
            Log_Debug("Track read failed\n");
        }
    }

    pause();

}
