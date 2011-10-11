#!/bin/sh
echo -e "Content-Type: text/html\r\n\r\n"

if [ $QUERY_STRING = "cls" ]; then
    echo -n > db.txt
else
    MSG=$(echo "$QUERY_STRING" | perl -pe "s/%([a-fA-F0-9]{2})/chr(hex(\$1))/eg")
    echo $MSG >> db.txt
fi

