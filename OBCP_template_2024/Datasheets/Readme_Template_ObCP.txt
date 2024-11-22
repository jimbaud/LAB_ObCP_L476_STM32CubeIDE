Author : Joel IMBAUD
SUPMICROTECH ENSMM www.supmicrotech.fr

From the Bluetooth Low Energy (BLE) connection:
 
This program works with the BLE terminal or other compatible with NRF UART standard... smartphone applications and the SUPMICROTECH ENSMM ObCP, 
Once the application is installed (search BLE terminal in playstore), the ObCP appears under the name CROC-OBCP in the BLE terminal connection search.

The accelerometer sensor reading can be sent every 1 second.

The commands to be sent from the application to control the ObCP are as follows: 
V or R or B or M for the command + the value from 0 to 65535
start timer : start the sending of acceleration values and temperature every second.
Stop timer : stop the sending
T : send the temperature 
A : send the accelerations
#rst : reset all the pwm and timers of the ObCP 
? : send all the command available

Ex : turn on green led of 20 : V20
Ex : turn off green led : V0
Ex : activate the pwm output at max : M65535

You also can use minuscule character.

From the Virtual Com Port (VCP) USB connection:

The commands are the same as for the BLE, in addition you have the command "cls" to clear the terminal console. 
IMPORTANT : the end emission character must be the Carrier Return (CR 0x0D) !!! It can be changed in the file code Driver_OBCP.c.

A Labiew application is given as an example of communication with the ObCP. (The VI file and the EXE file).

Have some fun with this template and don't hesitate to share your own code ;) 
