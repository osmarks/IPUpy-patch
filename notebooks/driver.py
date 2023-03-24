from ctypes import CDLL, c_char_p, c_int, c_ubyte, Structure, pointer, POINTER
import numpy as np
from numpy.ctypeslib import ndpointer

NUMREPLS = 100

multikernel = CDLL('./libmultikernel.so')
multikernel.init()

class KernelResult(Structure):
    _fields_ = [("count", c_int), ("data", c_char_p)]

execute = multikernel.execute
execute.argtypes = [c_char_p]
execute.restype = c_int

getResult = multikernel.getResult
getResult.restype = KernelResult

result = execute('print("Hello", __tileid % 3)\n'.encode())
print(result)

while True:
    result = getResult()
    if result.count == 0:
        break
    print(result.count, result.data)

getDisplay = multikernel.getDisplay
getDisplay.restype = POINTER(c_ubyte * (4 * NUMREPLS))

execute('setPixel(0, 0, 9)\n'.encode())
response = getDisplay()
vals = np.array([i for i in response.contents], dtype=np.uint8).reshape((-1, 4))
colours = vals[:, :3]
print(colours[:5])
