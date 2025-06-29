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

```
UNNE-IoT-PIR/
├── firmware/             # Código del dispositivo físico (sensor)
├── mqtt_service/         # Servicio Python para escuchar y guardar datos MQTT
├── dashboard/            # Aplicación Streamlit para visualización de datos
├── gateway/              # Archivos de configuración y notas del Gateway LoRa
├── data/                 # Almacenamiento centralizado de datos (CSV, etc.)
├── docs/                 # Documentación del proyecto
│   ├── history/          # Historial de cambios del proyecto (CHANGELOG)
│   └── circuit_diagrams/ # Esquemas de circuitos y documentación de hardware
├── .gitignore            # Archivo de ignorados de Git
└── README.md             # Descripción general del proyecto
```

## Registro de Cambios

Para ver un historial detallado de los cambios y la evolución de la estructura del proyecto, consulta el [CHANGELOG](docs/history/CHANGELOG.md).