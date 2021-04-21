#!/bin/bash
#
#

while true
do
	sleep 5
	printf "%s\t" "`date '+%Y-%m-%d %H:%M:%S'`"

	DATA=`timeout 5 GET http://192.168.1.126/data.json | tr -d '"' | awk '$2!=""{print d"  "$2}'`

	if [ -n "${DATA}" ];then
		echo "${DATA}"
		continue
	fi

	echo "COMMUNICATION PROBLEM"

done
