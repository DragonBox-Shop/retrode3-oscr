#!/bin/bash
# gadget config script for retread-controller-gadget
#

set -e

. /etc/gadget/core/libgadget.sh

# summary of how this script can be called:
#        * <gadget> `start
#        * <gadget> `stop
#        * <gadget> `status

if [ "$(which systemctl)" ]
then
	SERVICE=hidb@usb0.service
else
	SERVICE=hid
fi


case "$1" in
	start)
		#hid
		if [ "$(which systemctl)" ]
		then
			systemctl start $SERVICE
		else
			daemon -r --name=$SERVICE -- /etc/gadget/hidd hid0
		fi
		;;
	stop)
		if [ "$(which systemctl)" ]
		then
			systemctl stop $SERVICE
		else
			daemon --name=$SERVICE --stop 2>/dev/null
		fi
		ifconfig usb0 down || : ignore error
		remove_function ncm || echo failed to remove ncm gadget
#		remove_function ecm || echo failed to remove ecm gadget
#		remove_function rndis || echo failed to remove rndis gadget
		;;
	status)
# FIXME: dhcpd status
		if [ "$(which systemctl)" ]
		then
			echo "$SERVICE: $(systemctl is-active $SERVICE)"
		else
			echo "$SERVICE: $(ps -ef | fgrep -v grep | fgrep -q "daemon -r --name=$SERVICE" && echo running)"
		fi
		;;
	*)
		echo "$0 called with unknown argument \`$1'" >&2
		exit 1
	;;
esac

exit 0
