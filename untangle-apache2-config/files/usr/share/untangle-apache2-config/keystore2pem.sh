#! /bin/bash

set -e

KEYSTORE=$1
PEM=$2
ALIAS=${3:=`hostname`}

PASSWORD=changeit
der=`mktemp`
keytool -export -alias $ALIAS -keystore $KEYSTORE -storepass $PASSWORD -file $der

openssl x509 -in $der -inform der -out $PEM -outform pem
rm -f $der

java -classpath `dirname $0`/java ExportPriv $KEYSTORE $ALIAS $PASSWORD >> $PEM
