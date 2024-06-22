def run_client():
    import os
    import sys
    import logging
    import paho.mqtt.client as mqtt
    import json
    import csv
    import random
    from datetime import datetime
    import pytz

    # Procedure to get the USER, PASSWORD, PUBLIC_TLS_ADDRESS and PUBLIC_TLS_ADDRESS_PORT:
    # 1. Login to The Things Stack Community Edition console
    #    https://console.cloud.thethings.network/
    # 2. Select Go to applications
    # 3. Select your application
    # 4. On the left hand side menu, select Integrations | MQTT
    # 5. See Connection credentials
    # 6. For the password press button: Generate new API key
    #    Each time you press this button a new password is generated!
    #    The password looks like:
    #    NNSXS.XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX.XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
    #


    USER = "firework@ttn"
    PASSWORD = "NNSXS.FMGMPBTMKXQ5ZRB3LKWMH7X7PWYN34VQ3VTVUCA.SE35UDJS6X4CF5N2CAXVMLLYEBUOFBBK5JEIOQCQ4EIN3DNYLVFQ"
    PUBLIC_TLS_ADDRESS = "nam1.cloud.thethings.network"
    PUBLIC_TLS_ADDRESS_PORT = 8883
    DEVICE_ID = "eui-70b3d57ed005de4f"
    ALL_DEVICES = False

    # Meaning Quality of Service (QoS)
    # QoS = 0 - at most once
    # The client publishes the message, and there is no acknowledgement by the broker.
    # QoS = 1 - at least once
    # The broker sends an acknowledgement back to the client.
    # The client will re-send until it gets the broker's acknowledgement.
    # QoS = 2 - exactly once
    # Both sender and receiver are sure that the message was sent exactly once, using a kind of handshake
    QOS = 0

    DEBUG = True


    def get_value_from_json_object(obj, key):
        try:
            return obj[key]
        except KeyError:
            return '-'


    def stop(client):
        client.disconnect()
        print("\nExit")
        sys.exit(0)


    # Write uplink to tab file
    def save_to_file(some_json):
        end_device_ids = some_json["end_device_ids"]
        device_id = end_device_ids["device_id"]
        application_id = end_device_ids["application_ids"]["application_id"]
        received_at = some_json["received_at"]

        if 'uplink_message' in some_json:
            uplink_message = some_json["uplink_message"]
            f_port = get_value_from_json_object(uplink_message, "f_port")

            # check if f_port is found
            if f_port != '-':

                f_cnt = get_value_from_json_object(uplink_message, "f_cnt")
                frm_payload = uplink_message["frm_payload"]

                # If decoded_payload is a json object or a string "-" it will be converted to string
                decoded_payload = str(get_value_from_json_object(uplink_message, "decoded_payload"))
                rssi = get_value_from_json_object(uplink_message["rx_metadata"][0], "rssi")
                snr = get_value_from_json_object(uplink_message["rx_metadata"][0], "snr")
                data_rate_index = get_value_from_json_object(uplink_message["settings"], "data_rate_index")
                consumed_airtime = get_value_from_json_object(uplink_message, "consumed_airtime")

                # Daily log of uplinks
                now = datetime.now(pytz.timezone('America/Buenos_Aires'))
                path_data_folder = r'./data/'
                if not os.path.exists(path_data_folder):
                    os.mkdir(path_data_folder)
                path_n_file = path_data_folder + now.strftime("%Y%m%d") + ".csv"
                print(path_n_file)
                if not os.path.isfile(path_n_file):
                    with open(path_n_file, 'a', newline='') as csvFile:
                        fw = csv.writer(csvFile)
                        fw.writerow(["received_at", "application_id", "device_id", "f_port", "f_cnt", "rssi", "snr",
                                    "data_rate_index", "consumed_airtime", "frm_payload", "decoded_payload"])

                with open(path_n_file, 'a', newline='') as csvFile:
                    fw = csv.writer(csvFile)
                    fw.writerow([received_at, application_id, device_id, f_port, f_cnt, rssi, snr,
                                data_rate_index, consumed_airtime, frm_payload, decoded_payload])


    # The callback for when the client receives a CONNACK response from the server.
    def on_connect(client, userdata, flags, rc):
        if rc == 0:
            print("\nConnected successfully to MQTT broker")
        else:
            print("\nFailed to connect, return code = " + str(rc))


    # The callback for when a PUBLISH message is received from the server.
    def on_message(client, userdata, message):
        print("\nMessage received on topic '" + message.topic + "' with QoS = " + str(message.qos))

        parsed_json = json.loads(message.payload)

        if DEBUG:
            print("Payload (Collapsed): " + str(message.payload))
            print("Payload (Expanded): \n" + json.dumps(parsed_json, indent=4))

        save_to_file(parsed_json)


    # mid = message ID
    # It is an integer that is a unique message identifier assigned by the client.
    # If you use QoS levels 1 or 2 then the client loop will use the mid to identify messages that have not been sent.
    def on_subscribe(client, userdata, mid, granted_qos):
        print("\nSubscribed with message id (mid) = " + str(mid) + " and QoS = " + str(granted_qos))


    def on_disconnect(client, userdata, rc):
        print("\nDisconnected with result code = " + str(rc))


    def on_log(client, userdata, level, buf):
        print("\nLog: " + buf)
        logging_level = client.LOGGING_LEVEL[level]
        logging.log(logging_level, buf)


    # Generate client ID with pub prefix randomly
    client_id = f'python-mqtt-{random.randint(0, 1000)}'

    print("Create new mqtt client instance")
    mqttc = mqtt.Client(callback_api_version=mqtt.CallbackAPIVersion.VERSION1, 
                        client_id=client_id)

    print("Assign callback functions")
    mqttc.on_connect = on_connect
    mqttc.on_subscribe = on_subscribe
    mqttc.on_message = on_message
    mqttc.on_disconnect = on_disconnect
    mqttc.on_log = on_log  # Logging for debugging OK, waste

    # Setup authentication from settings above
    mqttc.username_pw_set(USER, PASSWORD)

    # IMPORTANT - this enables the encryption of messages
    mqttc.tls_set()  # default certification authority of the system

    # mqttc.tls_set(ca_certs="mqtt-ca.pem") # Use this if you get security errors
    # It loads the TTI security certificate. Download it from their website from this page: 
    # https://www.thethingsnetwork.org/docs/applications/mqtt/api/index.html
    # This is normally required if you are running the script on Windows

    print("Connecting to broker: " + PUBLIC_TLS_ADDRESS + ":" + str(PUBLIC_TLS_ADDRESS_PORT))
    mqttc.connect(PUBLIC_TLS_ADDRESS, PUBLIC_TLS_ADDRESS_PORT, 60)


    if ALL_DEVICES:
        print("Subscribe to all topics (#) with QoS = " + str(QOS))
        mqttc.subscribe("#", QOS)
    elif len(DEVICE_ID) != 0:
        topic = "v3/" + USER + "/devices/" + DEVICE_ID + "/up"
        print("Subscribe to topic " + topic + " with QoS = " + str(QOS))
        mqttc.subscribe(topic, QOS)
    else:
        print("Can not subscribe to any topic")
        stop(mqttc)


    print("And run forever")
    try:
        run = True
        while run:
            mqttc.loop(10)  # seconds timeout / blocking time
            print(".", end="", flush=True)  # feedback to the user that something is actually happening
    except KeyboardInterrupt:
        stop(mqttc)