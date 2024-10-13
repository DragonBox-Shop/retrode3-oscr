#!/bin/bash -e
#
# this script should be called by udev
# - on card/cart insertion/removal
# - on game controller connect/disconnect
#
# ACTION="change"		# currently we only have change events
# DEVNAME=/dev/slot0		# the device name of the slot (for slots)
# DEVNAME=""			# for game controller slot
# CHANNEL=0 / 1			# for game controller
# PWD=/				# script runs in root directory
# STATE=			# new device state
#
# check what happens with udevadm monitor --udev --property
#

echo DATE=$(date) SUBSYSTEM=$SUBSYSTEM ACTION=$ACTION DEVNAME=$DEVNAME STATE=$STATE @: "$@" >>/tmp/udev-retrode3.log
# set >>/tmp/udev-retrode3.log

case "$ACTION" in
	change )
		if [ "$DEVNAME" ]
		then # real slot with /dev/slot0..2

			SLOT=$(basename "$DEVNAME")
			SENSENAME=/sys/class/retrode3/$SLOT/sense
			LEDNAME=/sys/class/leds/green:programming-${SLOT##slot}

			SENSE=$(cat "$SENSENAME" 2>/dev/null)
			case "$SENSE" in	# SENSE="active" / "empty"	# (new) state
				active )
					# make cart visible over USB (configfs)
					# also run C code to handle different addressing magic...
					echo default-on >$LEDNAME/trigger
					;;
				empty )
					# remove cart from USB (configfs)
					echo heartbeat >$LEDNAME/trigger
					;;
			esac
		else # game controller
			# decode CHANNEL 0 -> right, 1 -> left
			case "$CHANNEL" in
# FIXME: we should add something to the retrode3.rule so that the DEV is passed here
				0 ) CH=right; DEV=/dev/input/event1;;
				1 ) CH=left; DEV=/dev/input/event2;;
			esac
			LEDNAME=/sys/class/leds/red:heartbeat

			case "$STATE" in	# STATE="connected" / "disconnected"	# (new) state
				connected )
					ln -sf "$DEV" "/dev/input/$CH"
					# make cart visible over USB (configfs)
					# also run C code to handle different addressing magic...
					echo default-on >$LEDNAME/trigger
					;;
				disconnected )
					rm "/dev/input/$CH"
					# remove cart from USB (configfs)
					# FIXME: check other game controller and turn off only if both are disconnected
					echo heartbeat >$LEDNAME/trigger
					;;
			esac
		fi
		;;
	add )
		;;
	remove )
		;;
esac

