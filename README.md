# DS1307-RTC-drift-correction
I own many DS1307 chips, they are very cheap and I like to use in different light projects which need the date and/or the time. But I could not imagine that this RTC accuracy could be so bad. Of course it is possible to use another chip, but I do not want to rebuild my old projects hardware.

If accuracy is bad, it is however constant: few seconds per day (that can makes several minutes after few months). As the DS1307 owns a small 56 bytes NVRAM (data are kept as long as the battery stays in place) the idea of this project is to measure the drift, and record the drift value and the reference date into the NVRAM.

How it works

1- We will use the first 9 bytes of the DS1307 NVRAM:

- “cell0”and “cell1” are the 2 bytes which define the state of the DS1307:

- “cell0= 1 & cell1 = 1” the DS1307 drift operation completed

 - “cell0= 0 & cel1 = 0” the DS1307 drift operation is in progress – duration is 24hours

 - Any other value : drift operation has never been performed

-   “cell8”stores the drift value defined in 24 hours, it is a number of seconds. 

-    “cell2 to cell7” keep the date/time of the last drift operation. These data define the number of days to apply the drift value (which is calculated within 1 day): GoodTime = ReadTime - (numberOfDays x drift)

2- We use a date/time reference to compare with RTC and define the drift

This date/time reference can provide either from NTP – easy to get with the ESP8266MPU – or from a GPS module.

3- When a DS1307 is plugged there are 3 possible situations:

1- The DS1307 has never been tested. If operation is confirmed the flagged date/time (= current date/time + 24 hours) is stored in RAM, “cell0” and “cell1” are set to ‘0’.

2- The DS1307 is under test as long as “cell0”and “cell1” are set to ‘0’. Every minute the RTC number of seconds is compared to NTP’s which define the drift. We will suppose the drift will not exceed the range from -30s / +30s.As soon as the flagged date/time is reached ( so 24 hours later ) the DS1307 is supposed to be tested, the drift value is stored in “cell8”, “cell0” and “cell1”are set to ‘1’.

3- The DS1307 is yet tested and ready for use. First the time adjust is proposed, then to redo the test for another 24 hours.

It is not use to let the shield under power during the test. The reference date/time is get at every power on. If the next power on is done later than the required 24hours test, the drift value is corrected to reflect the 24 hours duration.

NOTE : 2 codes are shared:

The code for a wemos mini to detect and set the drift of any DS1307,

The code for an Arduino Uno to show how to use the DS1307 once its drift value has been checked 
