import os
import json
import time
import threading
import datetime
from connect_db import conectar_db
from dotenv import load_dotenv
from awscrt import mqtt5
from awsiot import mqtt5_client_builder

load_dotenv()

endpoint_iot = os.getenv("AWS_IOT_ENDPOINT")
port_iot = os.getenv("AWS_IOT_PORT")
cert_iot = os.getenv("AWS_CERT_CRT")
priv_iot = os.getenv("AWS_CERT_PRIVATE")
ca_iot = os.getenv("AWS_CERT_CA")
topic_publish = os.getenv("AWS_IOT_PUBLISH_TOPIC")
topic_subscribe = os.getenv("AWS_IOT_SUBSCRIBE_TOPIC")

mydb = conectar_db()
cursor = mydb.cursor()

connection_success_event = threading.Event()

def on_publish_received(publish_data):
    payload_raw = publish_data.publish_packet.payload
    topic = publish_data.publish_packet.topic
    print(f"Mensaje recibido de topic: [{topic}]: {payload_raw}")

    try:
        payload = json.loads(payload_raw)
    except:
        print("Error JSON")
        return

    procesar_registro(payload)

def on_lifecycle_connection_success(data):
    print("Conectado a AWS IoT")
    connection_success_event.set()

def on_lifecycle_attempting_connect(data):
    print("Intentando conectar a AWS IoT...")

def procesar_registro(payload):
    print("Procesando:", payload)
    uid = payload["uid"]
    zona = payload["zona"]

    cursor.execute("SELECT id_targeta FROM targeta WHERE codi_targeta=%s", (uid,))
    res = cursor.fetchone()

    if not res:
        print("Tarjeta no encontrada")
        enviar_respuesta("ERROR", uid, zona, "Tarjeta no registrada")
        return

    id_targeta = res[0]

    if not res:
        print("Zona no encontrada")
        enviar_respuesta("ERROR", uid, zona, "Zona no registrada")

    id_zona = res[0]
    data_hora = datetime.datetime.now()

    sql_registre = "INSERT INTO registre (id_zona, id_targeta, resultat, data_registre) VALUES (%s, %s, %s, %s)"
    val_registre = (id_zona, id_targeta, "PRESENT", data_hora)
    cursor.execute(sql_registre, val_registre)

    mydb.commit()

    print("Registrado insertado correctamente")
    enviar_respuesta("OK", uid, zona, "Asistencia registrada")

def enviar_respuesta(resultado, uid, zona, mensaje):
    data = {
        "resultado": resultado,
        "uid": uid,
        "zona": zona,
        "mensaje": mensaje
    }

    print("\nEnviando respuesta:", data)
    client.publish(
        mqtt5.PublishPacket(
            topic=topic_publish,
            payload=json.dumps(data).encode(),
            qos=mqtt5.QoS.AT_LEAST_ONCE
        )
    )

if __name__ == "__main__":

    print("\nCreando cliente MQTT5...")

    client = mqtt5_client_builder.mtls_from_path(
        endpoint = endpoint_iot,
        port=int(port_iot),
        cert_filepath = cert_iot,
        pri_key_filepath = priv_iot,
        ca_filepath = ca_iot,
        client_id = "py-controlcat",
        on_publish_received = on_publish_received,
        on_lifecycle_connection_success = on_lifecycle_connection_success,
        on_lifecycle_attempting_connect = on_lifecycle_attempting_connect
    )

    print("Iniciando cliente...")
    client.start()

    if not connection_success_event.wait(10):
        print("No se pudo conectar a AWS IoT")
        exit(1)

    print("Suscribiéndose a {topic_subscribe}")

    sub_future = client.subscribe(
        mqtt5.SubscribePacket(
            subscriptions=[mqtt5.Subscription(topic_filter=topic_subscribe, qos=mqtt5.QoS.AT_LEAST_ONCE)]
        )
    )

    print("Suscripción completada")
    print("\nEsperando lecturas RFID...\n")

    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("Cerrando programa...")
        cursor.close()
        mydb.close()
        client.stop()
