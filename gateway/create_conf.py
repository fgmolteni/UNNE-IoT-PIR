#!/usr/bin/env python3
"""
Crea global_conf.json para AU915 sub-banda 2 (Argentina/Australia).
Canales uplink: 916.8 - 918.2 MHz (125 kHz, SF7-SF12)
Canal std:      917.5 MHz (500 kHz, SF8)
Downlink RX2:   923.3 MHz
Servidor:       TTN AU1 cluster
"""
import json, os

GATEWAY_ID = "0016C001FF1E02BA"
DEST = "/home/gabi/sx1302_hal/packet_forwarder/global_conf.json"

conf = {
    "SX130x_conf": {
        "com_type": "SPI",
        "com_path": "/dev/spidev0.0",
        "lorawan_public": True,
        "clksrc": 0,
        "antenna_gain": 0,
        "full_duplex": False,
        "fine_timestamp": {
            "enable": False,
            "mode": "all_sf"
        },
        "sx1261_conf": {
            "spi_path": "/dev/spidev0.1",
            "rssi_offset": 0,
            "spectral_scan": {
                "enable": False,
                "freq_start": 916800000,
                "nb_chan": 8,
                "nb_scan": 2000,
                "pace_s": 10
            },
            "lbt": {
                "enable": False
            }
        },
        # Radio 0: centro 917.1 MHz → cubre 916.8, 917.0, 917.2, 917.4 MHz
        "radio_0": {
            "enable": True,
            "type": "SX1250",
            "freq": 917100000,
            "rssi_offset": -215.4,
            "rssi_tcomp": {
                "coeff_a": 0, "coeff_b": 0,
                "coeff_c": 20.41, "coeff_d": 2162.56, "coeff_e": 0
            },
            "tx_enable": True,
            "tx_freq_min": 915000000,
            "tx_freq_max": 928000000,
            "tx_gain_lut": [
                {"rf_power": 12, "pa_gain": 0, "pwr_idx": 15},
                {"rf_power": 13, "pa_gain": 0, "pwr_idx": 16},
                {"rf_power": 14, "pa_gain": 0, "pwr_idx": 17},
                {"rf_power": 15, "pa_gain": 0, "pwr_idx": 19},
                {"rf_power": 16, "pa_gain": 0, "pwr_idx": 20},
                {"rf_power": 17, "pa_gain": 0, "pwr_idx": 22},
                {"rf_power": 18, "pa_gain": 1, "pwr_idx": 1},
                {"rf_power": 19, "pa_gain": 1, "pwr_idx": 2},
                {"rf_power": 20, "pa_gain": 1, "pwr_idx": 3},
                {"rf_power": 21, "pa_gain": 1, "pwr_idx": 4},
                {"rf_power": 22, "pa_gain": 1, "pwr_idx": 5},
                {"rf_power": 23, "pa_gain": 1, "pwr_idx": 6},
                {"rf_power": 24, "pa_gain": 1, "pwr_idx": 7},
                {"rf_power": 25, "pa_gain": 1, "pwr_idx": 9},
                {"rf_power": 26, "pa_gain": 1, "pwr_idx": 11},
                {"rf_power": 27, "pa_gain": 1, "pwr_idx": 14}
            ]
        },
        # Radio 1: centro 917.9 MHz → cubre 917.6, 917.8, 918.0, 918.2 MHz
        "radio_1": {
            "enable": True,
            "type": "SX1250",
            "freq": 917900000,
            "rssi_offset": -215.4,
            "rssi_tcomp": {
                "coeff_a": 0, "coeff_b": 0,
                "coeff_c": 20.41, "coeff_d": 2162.56, "coeff_e": 0
            },
            "tx_enable": False
        },
        "chan_multiSF_All": {
            "spreading_factor_enable": [7, 8, 9, 10, 11, 12]
        },
        # AU915 sub-banda 2 — canales 8-15
        "chan_multiSF_0": {"enable": True, "radio": 0, "if": -300000},   # 916.8 MHz
        "chan_multiSF_1": {"enable": True, "radio": 0, "if": -100000},   # 917.0 MHz
        "chan_multiSF_2": {"enable": True, "radio": 0, "if":  100000},   # 917.2 MHz
        "chan_multiSF_3": {"enable": True, "radio": 0, "if":  300000},   # 917.4 MHz
        "chan_multiSF_4": {"enable": True, "radio": 1, "if": -300000},   # 917.6 MHz
        "chan_multiSF_5": {"enable": True, "radio": 1, "if": -100000},   # 917.8 MHz
        "chan_multiSF_6": {"enable": True, "radio": 1, "if":  100000},   # 918.0 MHz
        "chan_multiSF_7": {"enable": True, "radio": 1, "if":  300000},   # 918.2 MHz
        # Canal LoRa 500 kHz — 917.5 MHz (SF8)
        "chan_Lora_std": {
            "enable": True, "radio": 0, "if": 400000,
            "bandwidth": 500000, "spread_factor": 8,
            "implicit_hdr": False, "implicit_payload_length": 17,
            "implicit_crc_en": False, "implicit_coderate": 1
        },
        "chan_FSK": {
            "enable": False, "radio": 1, "if": 300000,
            "bandwidth": 125000, "datarate": 50000
        }
    },
    "gateway_conf": {
        "gateway_ID": GATEWAY_ID,
        "server_address": "au1.cloud.thethings.network",
        "serv_port_up": 1700,
        "serv_port_down": 1700,
        "keepalive_interval": 10,
        "stat_interval": 30,
        "push_timeout_ms": 100,
        "forward_crc_valid": True,
        "forward_crc_error": False,
        "forward_crc_disabled": False,
        "beacon_enabled": False
    }
}

with open(DEST, "w") as f:
    json.dump(conf, f, indent=4)

print(f"Escrito: {DEST}")
print(f"Gateway ID: {GATEWAY_ID}")
print("Canales AU915 sub-banda 2: 916.8 - 918.2 MHz + 917.5 MHz (500kHz)")
print("Servidor: au1.cloud.thethings.network:1700")
