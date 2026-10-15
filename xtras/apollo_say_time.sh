#!/bin/bash

# Uhrzeit und awk

# Uhrzeit z.B. 1608
totaltime=$(date +'%H%M')

# Allestunden:
hour_total=$(date +'%H%M'|awk '{print substr($1,1,2)}')

# Zehnerstunden:
hour_ten=$(date +'%H%M'|awk '{print substr($1,1,1)}')

# Einerstunden:
hour_single=$(date +'%H%M'|awk '{print substr($1,2,1)}')

# Alleminuten:
min_total=$(date +'%H%M'|awk '{print substr($1,3,2)}')

# Zehnerminuten:
min_ten=$(date +'%H%M'|awk '{print substr($1,3,1)}')

# Einerminuten:
min_single=$(date +'%H%M'|awk '{print substr($1,4,1)}')

# echo $hour_total
# echo $hour_ten
# echo $hour_single

# echo $min_total
# echo $min_ten
# echo $min_single

array_hour_ten=(zero teen twenty thirty fourty fifty sixty seventy eighty ninety)
array_hour_single=(zero one two three four five six seven eight nine)

array_min_ten=(zero teen twenty thirty fourty fifty sixty seventy eighty ninety)
array_min_single=(zero one two three four five six seven eight nine)

txt_hour_single=${array_hour_single[$hour_single]}
txt_hour_ten=${array_hour_ten[$hour_ten]}

txt_min_single=${array_min_single[$min_single]}
txt_min_ten=${array_min_ten[$min_ten]}


# DEBUG
# txt_hour_single='zero'
# txt_hour_ten='zero'

# txt_min_single='one'
# txt_min_ten='teen'
# min_total='11'

# ---------------------------------------------------------------------------------------------------------------------
# wenn Stunden gleich 0
if [[ $txt_hour_single = 'zero' && $txt_hour_ten = 'zero' ]]; then txt_hour_single=''; fi
# ---------------------------------------------------------------------------------------------------------------------
# wenn Minuten gleich 0
if [[ $txt_min_single = 'zero' && $txt_min_ten = 'zero' ]]; then txt_min_single='';  txt_min_ten=''; fi

if [[ $txt_min_single = '' && $txt_min_ten = '' ]]; then txt_and=''; else txt_and=' and '; fi
# ---------------------------------------------------------------------------------------------------------------------
# wenn Einerstunden gleich 0 = nur Zehnerstunden
if [[ $txt_hour_single = 'zero' ]]; then txt_hour_single=''; fi
# ---------------------------------------------------------------------------------------------------------------------
# wenn Einerminuten gleich 0 = nur Zehnerminuten
if [[ $txt_min_single = 'zero' ]]; then txt_min_single=''; fi
# ---------------------------------------------------------------------------------------------------------------------
# wenn Zehnerminuten gleich 0 = nur Einzelminuten
if [[ $txt_min_ten = 'zero' ]]; then txt_min_ten=''; fi
# ---------------------------------------------------------------------------------------------------------------------
# wenn Stunden gleich 10
if [[ $txt_hour_single = '' && $txt_hour_ten = 'teen' ]]; then txt_hour_single='';  txt_hour_ten='ten'; fi

# wenn Stunden gleich 11
if [[ $txt_hour_single = 'one' && $txt_hour_ten = 'teen' ]]; then txt_hour_single='';  txt_hour_ten='eleven'; fi

# wenn Stunden gleich 12
if [[ $txt_hour_single = 'two' && $txt_hour_ten = 'teen' ]]; then txt_hour_single='';  txt_hour_ten='twelve'; fi

# wenn Stunden gleich 13
if [[ $txt_hour_single = 'three' && $txt_hour_ten = 'teen' ]]; then txt_hour_single='';  txt_hour_ten='thirteen'; fi

# wenn Stunden gleich 15
if [[ $txt_hour_single = 'five' && $txt_hour_ten = 'teen' ]]; then txt_hour_single='';  txt_hour_ten='fifteen'; fi


# ---------------------------------------------------------------------------------------------------------------------
# wenn Minuten gleich 10
if [[ $txt_min_single = '' && $txt_min_ten = 'teen' ]]; then txt_min_single='';  txt_min_ten='ten'; fi

# wenn Minuten gleich 11
if [[ $txt_min_single = 'one' && $txt_min_ten = 'teen' ]]; then txt_min_single='';  txt_min_ten='eleven'; fi

# wenn Minuten gleich 12
if [[ $txt_min_single = 'two' && $txt_min_ten = 'teen' ]]; then txt_min_single='';  txt_min_ten='twelve'; fi

# wenn Minuten gleich 13
if [[ $txt_min_single = 'three' && $txt_min_ten = 'teen' ]]; then txt_min_single='';  txt_min_ten='thirteen'; fi

# wenn Minuten gleich 15
if [[ $txt_min_single = 'five' && $txt_min_ten = 'teen' ]]; then txt_min_single='';  txt_min_ten='fifteen'; fi

# ---------------------------------------------------------------------------------------------------------------------
if [[ $hour_total < 14 ]]
then
print_hour="Its now $txt_hour_single $txt_hour_ten  a clock"
fi


if [[ $hour_total > 13 && $hour_total < 20 ]]
then
print_hour="Its now $txt_hour_single $txt_hour_ten  a clock"
fi

if [[ $hour_total > 19 ]]
then
print_hour="Its now $txt_hour_ten $txt_hour_single a clock"
fi
# ---------------------------------------------------------------------------------------------------------------------

# ---------------------------------------------------------------------------------------------------------------------

if [[ $min_total < 10 ]]
then
 echo "Match 1"
print_min=" $txt_min_single $txt_min_ten minutes."
fi

# echo "MinTotal: $min_total"
if [[ $min_total > 09 && $min_total < 14 ]]
then
 echo "Match 2"
print_min=" $txt_min_ten minutes."
fi

if [[ $min_total = 15 ]]
then
 echo "Match 3"
print_min=" $txt_min_ten minutes."
fi

if [[ $min_total > 15 && $min_total < 20 ]]
then
 echo "Match 4"
print_min=" $txt_min_single $txt_min_ten minutes."
fi

if [[ $min_total > 19 ]]
then
 echo "Match 5"
print_min=" $txt_min_ten $txt_min_single minutes."
fi

if [[ $txt_min_ten = '' && $txt_min_single = '' ]]
then
 echo "Match 6"
print_min="."
fi

# ---------------------------------------------------------------------------------------------------------------------
stty -F /dev/ttyUSB0 9600 cs8 -cstopb -parenb
 echo $txt_hour_ten
 echo $txt_hour_single
 echo "TxtMin_Ten: $txt_min_ten"
 echo "TxtMin_Single: $txt_min_single"
echo $print_hour$txt_and$print_min
echo "@W2@V2$print_hour$txt_and$print_min." > /dev/ttyUSB0

