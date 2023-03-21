from ctypes import CDLL, c_char_p, c_int, Structure, pointer


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
