#!/usr/bin/env bash

KEY_FILE="ca_key.pem"
CERT_FILE="ca_cert.pem"
CONFIG_FILE="cert_config.cnf"
# OPENSSL_CMD="/usr/bin/env openssl"
COMMON_NAME=`hostname -I`

## Update Common Name in Configuration File
head -n -1 $CONFIG_FILE > $CONFIG_FILE.tmp
/bin/echo "commonName              = $COMMON_NAME" >> $CONFIG_FILE.tmp
mv $CONFIG_FILE.tmp $CONFIG_FILE
rm -f $CONFIG_FILE.tmp

# Create self-signed certificate and key
openssl req -x509 -newkey rsa:2048 -keyout $KEY_FILE -out $CERT_FILE -days 365 -nodes -config $CONFIG_FILE 2>/dev/null

if [ $? -ne 0 ] ; then
    echo "ERROR: Failed to generate self-signed certificate file $CERT_FILE"
else
    echo "SUCCESS: Generated self-signed certificate file $CERT_FILE"
fi
