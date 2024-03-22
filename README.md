# UNNE-IoT-PIR
Proyecto realizado por equipos de desarrollo de la Universidad Nacional del Nordeste (UNNE). Desarrollo de redes de nodos IoT mediante conexión LoRa, para la prevención de incendios rurales. 

## Directores
- Dra. Maria Ines Pisarello.
- Ing. Tito Chiozza.

## Equipo de Desarrollo
- Ing. Sosa Gaston Martin.
- Ing. Valentin Vizcaychipi.
- Facundo Gabriel Molteni Morales.


## Librerias Utilizadads
- MCCI LMIC
- DHT Sensor Library
- Heltec ESP32

## Plataforma de Desarrollo
- PlatformIO

## PLacas Soportadas
- Heltec WiFi LoRA V2


## Conexión por MQTT
Realizamos una conexión al los servidores de TTN mediante el montaje de un servidor MQTT, este enviara los datos desde los TTN a los "suscriptores", para ello realizamos un script en python.

primero debemos habilitar una API keys en TTN, es lo hacemos yendo a la aplicación de nuestro dispositivo. Seguidamente nos vamos al apartado **Integraciones>>MQTT** y añadimos una nueva api-keys.


## Observaciones

Utilizar version 1 de mqtt

## Estructura

		Proyecto
		|
		+---->	Firmware Nodo-Red
		|		|
		|		+--->	Sensores.
		|		|
		|		+--->	Display OLED. 
		|		|
		|		+--->	Envió de Datos.
		|
		+---->	Cliente MQTT
				|
				+--->	*Almacenamiento*
				|
				+--->	*Visualizacion*