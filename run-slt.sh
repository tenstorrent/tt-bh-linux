#!/bin/bash

while [ /bin/true ];
do
	for ((L2CPU=0; L2CPU<=3; L2CPU++))
	do
		LOG="l2cpu_$L2CPU.log"
		echo "L2CPU $L2CPU: START"
		if [ -f "$LOG" ]; then
			mv $LOG ${LOG}.$(date +%Y%m%d_%H%M%S)
		fi
		cp -p ~/buildroot/output/images/rootfs.ext4 rootfs.ext4
		md5sum rootfs.ext4
		L2CPU=$L2CPU make boot &> $LOG &
		for ((i=1; i<=60; i++))
		do
			RESULT=$(grep 'successful run completed' $LOG)
			if [ "$RESULT" == "" ]; then
				echo -n "."
				sleep 1
			else
				echo -e "\nRESULT: $RESULT"
				pkill tt-bh-linux
				echo complete
				break
			fi
	
	
		done
		echo "L2CPU $L2CPU: END"
		sleep 1
	done
	# TODO: change this to whatever is actually needed for production env
	echo -n "Insert next Blackhole SoC to test..."
	# wait 10 seconds
	for ((i=1; i<=10; i++)); do echo -n "."; sleep 1; done
	echo ""
done
