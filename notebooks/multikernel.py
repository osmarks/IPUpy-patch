from ctypes import CDLL, c_char_p, c_int, Structure
from ipykernel.kernelbase import Kernel


class KernelResult(Structure):
    _fields_ = [("count", c_int), ("data", c_char_p)]
    

class MultiKernel(Kernel):
    implementation = 'MultiKernel'
    implementation_version = '0.1'
    language = 'IPUpython'
    language_version = '3.4'
    language_info = {
        'name': 'Any text',
        'mimetype': 'text/plain',
        'file_extension': '.txt',
    }
    banner = "IPUpython"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.backend = CDLL('./libmultikernel.so')
        self.backend.init()

        self.backend.execute.argtypes = [c_char_p]
        self.backend.getResult.restype = KernelResult

    def do_execute(self, code, silent, store_history=True, user_expressions=None,
                   allow_stdin=False):
        success = self.backend.execute(code.encode())
        if not silent:
            results = []
            while True:
                result = self.backend.getResult()
                if result.count == 0:
                    break
                results.append(f'\x1b[01;34m-------[{result.count}x]-------\x1b[0m\n{result.data.decode("utf-8")}')
            stream_content = {'name': 'stdout', 'text': '\n'.join(results)}
            self.send_response(self.iopub_socket, 'stream', stream_content)

        return {'status': 'ok',
                # The base class increments the execution count
                'execution_count': self.execution_count,
                'payload': [],
                'user_expressions': {},
               }

if __name__ == '__main__':
    from ipykernel.kernelapp import IPKernelApp
    IPKernelApp.launch_instance(kernel_class=MultiKernel)
