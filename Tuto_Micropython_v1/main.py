from pyb import *
import pyb
pc6 = Pin("PC6", Pin.OUT)

while True:
    pc6.value(1)
    pyb.delay(250)
    pc6.value(0)
    pyb.delay(250)
