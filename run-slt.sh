#!/bin/bash

while [ /bin/true ];
do
	for ((L2CPU=0; L2CPU<=3; L2CPU++))
	do
		LOG="l2cpu_$L2CPU.log"
		echo "L2CPU $L2CPU: START"
		L2CPU=$L2CPU make boot &> $LOG &
		for ((i=1; i<=60; i++))
		do
			RESULT=$(grep success $LOG)
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
done
