#!/bin/bash

SLEEP_SEC=60

if [ -z "$ACCT" ]; then
	echo 'Variable $ACCT is not set'
	exit
elif [ -z "$HOST" ]; then
	echo 'Variable $HOST is not set'
	exit
elif [ -z "$PORT" ]; then
	echo 'Variable $PORT is not set'
	exit
elif [ -z "$IMAP_USER" ]; then
	echo 'Variable $IMAP_USER is not set'
	exit
elif [ -z "$IMAP_PASS" ]; then
	echo 'Variable $IMAP_PASS is not set'
	exit
fi

CONF=$(cat <<EOF
action "maildir" maildir "%h/Maildir"
account "$ACCT" imap server "$HOST" port "$PORT" user "$IMAP_USER" pass "$IMAP_PASS"
match all action "maildir"
EOF
)

while true; do
	fdm -f <(echo "$CONF") -v fetch
    echo
	imap-idle-until-new
	status=$?

	echo; echo "Exit Status: $status"; echo
	if [ $status -eq 0 ]; then
		: # new mail arrived; loop immediately to fetch it
	elif [ $status -eq 1 ]; then
		echo "Sleeping for $SLEEP_SEC secs..."
		sleep $SLEEP_SEC
	elif [ $status -eq 2 ]; then
		exit
	else
		echo "Unexpected exit status $status, sleeping for $SLEEP_SEC secs..."
		sleep $SLEEP_SEC
	fi
done
