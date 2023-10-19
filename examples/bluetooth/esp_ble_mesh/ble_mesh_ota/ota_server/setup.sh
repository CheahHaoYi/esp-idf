#!/usr/bin/env bash

HOST_IP=`hostname -I | xargs`
PORT=8070
OTA_BINARY='ble_mesh_ota.bin'
CONFIG_VALUE='CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL'

OTA_URL="http://$HOST_IP:$PORT/$OTA_BINARY"

echo "OTA file is hosted at: $OTA_URL"

cd server_certs
bash generate_cert.sh
cd ..

echo "ca_cert.pem and ca_key.pem generated in server_certs folder" 
