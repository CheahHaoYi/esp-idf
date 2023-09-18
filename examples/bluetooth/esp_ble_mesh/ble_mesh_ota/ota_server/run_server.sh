#!/usr/bin/env bash

BUILD_DIR="build"
KEY_FILE="ca_key.pem"
CERT_FILE="ca_cert.pem"
PORT=8070

cp server_certs/$KEY_FILE server_certs/$CERT_FILE $BUILD_DIR

cd $BUILD_DIR

echo "Starting Server"
openssl s_server -WWW -key $KEY_FILE -cert $CERT_FILE -port $PORT

