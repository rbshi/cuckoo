#!/bin/bash

rm *.log *.log2

timelog='time.log'
sollog='sol.log'
timelog2='time.log2'
sollog2='sol.log2'

for ((nedgebit=12; nedgebit<13; nedgebit++))
do
	echo '================NEDGEBIT='$nedgebit'================' >> $timelog
	echo '================NEDGEBIT='$nedgebit'================' >> $sollog	
	
	echo '================NEDGEBIT='$nedgebit'================'
	
	for ((proofsize=16; proofsize<60; proofsize+=4))
	do
		echo '================PROOFSIZE='$proofsize'================'
		
		echo '================PROOFSIZE='$proofsize'================' >> $timelog
		echo '================PROOFSIZE='$proofsize'================' >> $sollog
		
		# compilation
		g++ -march=native -std=c++11 -Wall -Wno-format -Wno-deprecated-declarations -D_POSIX_C_SOURCE=200112L -O3 -DPREFETCH -I.  -pthread -o lean.exe -DNSIPHASH=1 -DATOMIC -DEDGEBITS=$nedgebit -DPROOFSIZE=$proofsize lean.cpp ../crypto/blake2b-ref.c	
		
		# execution
		for ((nonce=0; nonce<2000000; nonce+=10))
		do
			# echo 'nonce='$nonce
			exestring=$(./lean.exe -n $nonce)
			tmp=${exestring%%Time*}
			tmpidx1=$((${#tmp}))
			tmp=${exestring%%us*}
			tmpidx2=$((${#tmp}))
			echo -en ${exestring:$tmpidx1+6:$tmpidx2-$tmpidx1-6}'\t' >> $timelog
			echo -en ${exestring:$tmpidx1+6:$tmpidx2-$tmpidx1-6}'\t' >> $timelog2
			tmp=${exestring%%total*}
			tmpidx3=$((${#tmp}))
			echo -en ${exestring:$tmpidx2+3:$tmpidx3-$tmpidx2-4}'\t' >> $sollog
			echo -en ${exestring:$tmpidx2+3:$tmpidx3-$tmpidx2-4}'\t' >> $sollog2
			
		done	
		echo -en '\n' >> $timelog
		echo -en '\n' >> $sollog
		echo -en '\n' >> $timelog2
		echo -en '\n' >> $sollog2
	done
	
done

