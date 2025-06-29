# Registro de Cambios del Proyecto UNNE-IoT-PIR

Este documento detalla los cambios estructurales y de configuración realizados en el proyecto UNNE-IoT-PIR para mejorar su organización, mantenibilidad y escalabilidad.

## 1. Reestructuración de Directorios (Orientada a Servicios)

Se implementó una reestructuración de directorios para un enfoque más plano y orientado a servicios.

-   **Consolidación de componentes**: Módulos de `firmware`, `mqtt_service` y `dashboard` organizados en directorios dedicados.
-   **Centralización de datos**: El directorio `data/` establecido como punto centralizado para el almacenamiento de datos.
-   **Eliminación de `backend/`**: El directorio `backend/` fue eliminado tras la migración de su contenido.

## 2. Actualización del `.gitignore`

El archivo `.gitignore` fue actualizado y reorganizado para incluir reglas más robustas y claras.

-   **General & OS-Specific**: Reglas para archivos generados por sistemas operativos y logs.
-   **Development Environment**: Reglas para variables de entorno y configuraciones de VS Code.
-   **Python Specific**: Reglas para entornos virtuales, caché y artefactos de construcción/pruebas.
-   **PlatformIO & Firmware Specific**: Reglas para PlatformIO y artefactos de construcción/dependencias del firmware.

## 3. Configuración de Entornos Virtuales

Se estandarizó el nombre de los entornos virtuales de Python a `.venv/` (oculto) por servicio.

-   El entorno virtual `dashboard/venv` fue eliminado y recreado como `dashboard/.venv`.

## 4. Actualizaciones del Agente de Desarrollo (28 de junio de 2025)

-   **Configuración de contexto**: Se estableció un archivo de configuración para el agente de desarrollo.
-   **Detalles del proyecto**: Se incluyó la descripción, estructura, tecnologías y comandos de ejecución del proyecto.
-   **Refinamiento de información**: Se realizaron ajustes y correcciones en la información de configuración del agente.
-   **Definición de proceso de documentación**: Se estableció un mecanismo para que el agente documente sus acciones en el registro de cambios.

## 5. Gestión de Archivos de VS Code

Se optimizó la gestión de los archivos de configuración de Visual Studio Code en el repositorio.

-   **Exclusión de archivos locales**: Se actualizó el `.gitignore` para excluir configuraciones locales y de caché (`.vscode/*`), previniendo que archivos específicos del entorno de un desarrollador sean subidos al repositorio.
-   **Inclusión de configuraciones recomendadas**: Se mantuvieron los archivos `settings.json` y `extensions.json` en el control de versiones para asegurar que todos los colaboradores utilicen una configuración base coherente y las extensiones recomendadas para el proyecto.

---

Este registro se mantendrá actualizado con futuros cambios significativos en la estructura o configuración del proyecto.
