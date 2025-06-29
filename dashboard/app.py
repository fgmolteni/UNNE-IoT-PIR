from module import mqtt

import streamlit as st
import pandas as pd
import os

st.set_page_config(
    page_title="UNNE-IoT-PIR",
    page_icon="https://raw.githubusercontent.com/UNNE-IoT/UNNE-IoT-PIR/main/img/favicon.ico",
    layout="wide",
    initial_sidebar_state="expanded",
    menu_items={
        'Get Help': 'https://www.unne.edu.ar',
        'Report a bug': "https://www.unne.edu.ar",
        'About': "# Dashboard para visualizar datos de un sensor PIR."
    }
)

st.title('Visor de datos del sensor')

# Función para cargar los datos desde los archivos CSV
def load_data(data_folder):
    all_files = [os.path.join(data_folder, f) for f in os.listdir(data_folder) if f.endswith('.csv')]
    if not all_files:
        return pd.DataFrame()
    df_list = [pd.read_csv(file) for file in all_files]
    combined_df = pd.concat(df_list, ignore_index=True)
    combined_df = combined_df.sort_values(by='received_at', ascending=False)
    return combined_df

# Carpeta donde se guardan los datos
DATA_FOLDER = '../data'

# Cargar los datos
if not os.path.exists(DATA_FOLDER):
    st.error(f"La carpeta de datos no existe en la ruta esperada: {os.path.abspath(DATA_FOLDER)}")
    st.info("Asegúrese de que el servicio MQTT se esté ejecutando y haya guardado algunos datos.")
    st.stop()

data = load_data(DATA_FOLDER)

if data.empty:
    st.warning("No se encontraron archivos de datos CSV. Asegúrese de que el servicio MQTT esté en funcionamiento y guardando datos.")
else:
    st.dataframe(data)
