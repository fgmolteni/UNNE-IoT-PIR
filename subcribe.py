import paho.mqtt.client as mqtt
from datetime import datetime as datetime
import random

# Reemplace con sus valores
app_id = "firework@ttn"
access_key = "NNSXS.UTIGWWXDBPMH7CLWZAP65JM7HGELPXZAQ4V56LQ.AUGGSOXTUGAZJVQSVDYSIJVK7TTCDQPVSQZYIFEKY4L7YC5YOLTA"
device_eui = "eui-70b3d57ed005de4f"
public_address = "nam1.cloud.thethings.network"
public_address_port = 1883
ALL_DEVICES = False
QOS = 0

def on_connect(client, userdata, flags, reason_code, properties):

    if rc == 0:
        print("\nConnected successfully to MQTT broker")
    else:
        print("\nFailed to connect, return code = " + str(rc))

    print(f'Conectado con codigo {reason_code}')
    client.subscribe("#")

def on_message(client, userdata, msg):
    print(msg.topic+" "+str(msg.payload))

mqttc = mqtt.Client()
mqttc.on_connect
mqttc.on_message = on_message

mqttc.connect(public_address, public_address_port, 60)
mqttc.loop_forever()