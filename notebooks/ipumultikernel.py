from ctypes import CDLL, c_char_p, c_int, c_ubyte, Structure, POINTER
from io import BytesIO
from math import sqrt
import urllib, base64
from time import sleep
import re

from ipykernel.kernelbase import Kernel
import numpy as np
from PIL import Image


class KernelResult(Structure):
    _fields_ = [("count", c_int), ("data", c_char_p)]
    

class IPUMultiKernel(Kernel):
    implementation = 'IPUMultiKernel'
    implementation_version = '0.1'
    language = 'python'
    language_version = '3.4'
    language_info = {
        'name': 'Any text',
        'mimetype': 'text/plain',
        'file_extension': '.txt',
    }
    banner = "IPUMultiKernel"

    def __init__(self, *args, **kwargs):
        self.backend = CDLL('./libmultikernel.so')
        self.backend.init()

        self.num_repls = self.backend.getNumRepls()
        
        self.backend.execute.argtypes = [c_char_p]
        self.backend.getResult.restype = KernelResult
        self.backend.getDisplay.restype = POINTER(c_ubyte * (4 * self.num_repls))

        self.display_h = int(sqrt(self.num_repls))
        while self.num_repls % self.display_h != 0 and self.display_h > 1:
            self.display_h -= 1
        self.display_w = self.num_repls // self.display_h
        self.display_scale = 500 / self.display_w

        self.repeatBlockRegex = re.compile('[\n \t]*!RepeatBlock\(([0-9]+)\)')
        self.endRepeatRegex = re.compile('[\n \t]*!EndRepeat')
        
        super().__init__(*args, **kwargs)

    def do_execute(self, code, silent, store_history=True, user_expressions=None,
                   allow_stdin=False):
        self.first_display = True
        
        lines = code.splitlines()
        current_lines, in_block, repetitions = [], False, None
        for ln_num, line in enumerate(lines):
            if not in_block:
                cmd = self.repeatBlockRegex.match(line)
                if cmd is None:
                    current_lines.append(line)
                    if ln_num == len(lines) - 1:
                        self.do_single_execute('\n'.join(current_lines))
                    continue
                if current_lines:
                    self.do_single_execute('\n'.join(current_lines))
                    current_lines.clear()
                repetitions = int(cmd.groups()[0])
                in_block = True
            else:
                if self.endRepeatRegex.match(line) is None:
                    current_lines.append(line)
                    if ln_num != len(lines) - 1:
                        continue
                block = '\n'.join(current_lines)
                for _ in range(repetitions):
                    # self.send_response(self.iopub_socket, 'clear_output', {'wait': True})
                    self.do_single_execute(block)

                in_block = False
                current_lines.clear()

        return {'status': 'ok', 'execution_count': self.execution_count,
                'payload': [], 'user_expressions': {}}

    def do_single_execute(self, code):
        success = self.backend.execute(code.encode())

        # Send text outputs
        results = []
        while True:
            result = self.backend.getResult()
            if result.count == 0:
                break
            if len(result.data) == 0:
                continue
            results.append(f'\x1b[01;34m-------[{result.count}x]-------\x1b[0m\n{result.data.decode("utf-8")}')
            if len(results) == 100:
                count = 0
                while self.backend.getResult().count != 0:
                    count += 1
                results.append(f'\x1b[01;31m-----[OVERFLOW, {count} UNIQUE RESULTS NOT SHOWN]-----\x1b[0m\n')
                break
        self.send_response(self.iopub_socket, 'stream', {'name': 'stdout', 'text': '\n'.join(results)})

        # Send image outputs
        vals = np.array([i for i in self.backend.getDisplay().contents], dtype=np.uint8)
        vals = vals.reshape((self.display_h, self.display_w, 4))
        if not np.any(vals[..., 0]):
            return
        imgbuffer = BytesIO()
        Image.fromarray(vals[..., 1:]).save(imgbuffer, format='png')
        imgbuffer.seek(0)
        msg_type = 'display_data' if self.first_display else 'update_display_data'
        self.first_display = False
        self.send_response(self.iopub_socket, msg_type, {
            'source': 'kernel',
            'data': {
                'image/png': urllib.parse.quote(base64.b64encode(imgbuffer.getvalue()))
            },
            'metadata' : {
                'image/png' : {
                    'width': int(self.display_w * self.display_scale),
                    'height': int(self.display_h * self.display_scale)
                }
            },
            'transient' : {
                'display_id': f'setPixel{self.execution_count}'
            }
        })


if __name__ == '__main__':
    from ipykernel.kernelapp import IPKernelApp
    IPKernelApp.launch_instance(kernel_class=IPUMultiKernel)
