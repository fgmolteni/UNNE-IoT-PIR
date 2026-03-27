# UNNE-IoT-PIR - Sistema IoT de Prevencion de Incendios Rurales

Esta rama contiene la migracion del firmware de la placa `Heltec WiFi LoRa 32 V2` a `ESP-IDF puro`.

## Estado de la rama

Primera iteracion completada para el firmware:

- estructura ESP-IDF limpia
- OLED integrada funcionando con driver SSD1306 propio
- lectura de DHT11
- logs serie con `ESP_LOGI`

Todavia no incluye TTN ni LoRaWAN. Esa integracion queda como siguiente fase.

## Directorios relevantes

- `firmware/`: firmware ESP-IDF puro para la Heltec V2
- `docs/`: documentacion del proyecto y diagramas

## Flujo de trabajo del firmware

```bash
cd firmware
idf.py set-target esp32
idf.py build
idf.py flash monitor
```

## Resumen de la migracion

- Se elimino la dependencia de PlatformIO para el firmware de esta rama.
- Se elimino la dependencia de Arduino para OLED y DHT11.
- Se dejo una base liviana para continuar con TTN desde ESP-IDF puro.

## Proximo paso recomendado

- integrar TTN/LoRaWAN AU915 sobre la nueva arquitectura modular de `firmware/`
