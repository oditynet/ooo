mkdir del
count=10

for (( i=1; i<=$count; i++ ))
do
  head -c ${i}k </dev/urandom >del/f${i}.dat
  if [ $i -eq 1 ];then
          ./ooo -c out.ooo -b $(shuf -i 1-7 -n 1) del/f${i}.dat
  else
     ./ooo -a out.ooo -b $(shuf -i 1-7 -n 1) del/f${i}.dat
  fi
done
./ooo -d out.ooo del/f$(shuf -i 1-10 -n 1).dat
./ooo -v out.ooo
./ooo -d out.ooo del/f$(shuf -i 1-10 -n 1).dat
./ooo -d out.ooo del/f$(shuf -i 1-10 -n 1).dat
./ooo -v out.ooo
./ooo -d out.ooo del/f$(shuf -i 1-10 -n 1).dat
./ooo -d out.ooo del/f$(shuf -i 1-10 -n 1).dat
./ooo -d out.ooo del/f$(shuf -i 1-10 -n 1).dat
./ooo -v out.ooo
./ooo -d out.ooo del/f$(shuf -i 1-10 -n 1).dat
./ooo -d out.ooo del/f$(shuf -i 1-10 -n 1).dat
./ooo -v out.ooo
./ooo -x out.ooo del
#rm -rf del