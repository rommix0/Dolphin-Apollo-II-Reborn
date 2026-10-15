stty -F /dev/ttyUSB0 9600 cs8 -cstopb -parenb
echo "@W2@V2Hello Youtube. I am the Dolhin Apollo Two Speech Synthesizer." > /dev/ttyUSB0
sleep 4
echo "@W2@V2Now you will hear how my Voices would sound." > /dev/ttyUSB0
sleep 4
echo "@W2@V2OK Youtubers lets get started." > /dev/ttyUSB0
sleep 4
echo "@W2@V2I was build around Nineteenhundredninetythree." > /dev/ttyUSB0
sleep 4
echo "@W2@V2I got three male and three female voices." > /dev/ttyUSB0
sleep 4
echo "@W2@V2Here are my three male voices." > /dev/ttyUSB0
sleep 4
echo "@W2@V1This is my first male voice!" > /dev/ttyUSB0
sleep 4
echo "@W2@V2This is my second male voice!" > /dev/ttyUSB0
sleep 4
echo "@W2@V3And this is my third  male voice!" > /dev/ttyUSB0
sleep 6
echo "@W2@V2And now here are my three female voices." > /dev/ttyUSB0
sleep 4
echo "@W2@V4This is my first female voice!" > /dev/ttyUSB0
sleep 4
echo "@W2@V5This is my second female voice!" > /dev/ttyUSB0
sleep 4
echo "@W2@V6And this is my third female voice!" > /dev/ttyUSB0
sleep 4
echo "@W2@V2In my ROM I only got the UK voices, but the Dolphin Company did build also some other languages." > /dev/ttyUSB0
sleep 6
echo "@W2@V2I can be programmed via a RS232 Interface at ninethousandsixhundered baud." > /dev/ttyUSB0
sleep 4
echo "@W2@V2Normally I cant read numbers greater than nine - so only single numbers." > /dev/ttyUSB0
sleep 4
echo "@W2@V2But my new owner did wrote me a shell script. And now I can read the actual time with numbers greater than nine." > /dev/ttyUSB0
sleep 4
echo "@W2@V2Here is the result of the script which my new Owner did wrote for me" > /dev/ttyUSB0
sleep 10
/home/guido/time_convert.sh
sleep 6
echo "@W2@V2OK that was all I would tell you. Have a nice day! " > /dev/ttyUSB0
sleep 4
echo "@W2@V2It would be nice to see you again in the near future!" > /dev/ttyUSB0
