#!/bin/bash

SLEEP_SEC=60

if [ -z "$CERT_PATH" ]; then
	echo 'Variable $CERT_PATH is not set'
	exit
elif [ -z "$ACCT" ]; then
	echo 'Variable $ACCT is not set'
	exit
elif [ -z "$HOST" ]; then
	echo 'Variable $HOST is not set'
	exit
elif [ -z "$PORT" ]; then
	echo 'Variable $PORT is not set'
	exit
elif [ -z "$USER" ]; then
	echo 'Variable $USER is not set'
	exit
elif [ -z "$PASS" ]; then
	echo 'Variable $PASS is not set'
	exit
fi

CONF=$(cat <<EOF
action "maildir" maildir "%h/Maildir"
account "$ACCT" imaps server "$HOST" port "$PORT" user "$USER" pass "$PASS"
match all action "maildir"
EOF
)

while true; do
	fdm -f <(echo "$CONF") -v fetch
    echo
	imap-idle-until-new
	status=$?

	echo; echo "Exit Status: $status"; echo
	if [ $status -eq 1 ]; then
		echo "Sleeping for $SLEEP_SEC secs..."
		sleep $SLEEP_SEC
	elif [ $status -eq 2 ]; then
		exit
	fi
done
