#!/bin/bash

#usage: ./cert_renew.sh <key file> <cert file>

if [ "$#" -lt 2 ]
then
    echo "$0 <key file> <cert file> <number of days>"
    exit 1
fi

# First arg is pointed to current PEM file
key_file="$1"
cert_file="$2"

# Create temporary files
tmp_csr=$(mktemp /tmp/csr.XXXXXXXXX)

openssl x509 -x509toreq -in $cert_file -signkey $key_file -out $tmp_csr
openssl x509 -req -days 213700 -in $tmp_csr -signkey $key_file -out cert_new.pem
echo "new cert is in cert_new.pem"
rm $tmp_csr 
