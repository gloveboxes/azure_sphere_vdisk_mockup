import paho.mqtt.client as mqtt
import time

vdiskRequestMqttTopic = "altair/766786/vdisk/request"

response = ""

for i in range(1024 * 4):
    response = response + "a"

def on_connect(client, userdata, flags, rc):
    print("Connected with result code: %s" % rc)
    client.subscribe(vdiskRequestMqttTopic)


def on_disconnect(client, userdata, rc):
    print("Disconnected with result code: %s" % rc)


def on_message(client, userdata, msg):
    print("{0} - {1} ".format(msg.topic, str(msg.payload)))
    client.publish("altair/766786/vdisk/response", response)

client = mqtt.Client('altrair677868678', mqtt.MQTTv311)

client.on_connect = on_connect
client.on_disconnect = on_disconnect
client.on_message = on_message


client.connect("mosquitto.australiaeast.cloudapp.azure.com")

client.loop_start()


while True:  # sleep forever
    time.sleep(999999)
