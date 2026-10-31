# Third-party code in this directory

Everything else in this directory is by the repository author. The files
below contain or derive from code under the licenses reproduced here, and the
licenses require that these notices travel with the code.

## Raspberry Pi pico-examples and pico-sdk (BSD-3-Clause)

- `common/bootsel.c` derives from `picoboard/button/button.c`
  (`get_bootsel_button`) in [pico-examples](https://github.com/raspberrypi/pico-examples),
  by way of the [Bruce](https://github.com/cryptsoft/Bruce) firmware's
  `c/firmware/src/bootsel.c`, which kept the file under this same license.
- `pico_sdk_import.cmake` is a verbatim copy of `external/pico_sdk_import.cmake`
  from [pico-sdk](https://github.com/raspberrypi/pico-sdk).

```
Copyright 2020 (c) 2020 Raspberry Pi (Trading) Ltd.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the
following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following
   disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following
   disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products
   derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## TinyUSB example code (MIT)

- `03_keyboard_alphabet/usb_descriptors.c`, `03_keyboard_alphabet/tusb_config.h`,
  `03_keyboard_alphabet/keyboard_alphabet.c`, and their copies in
  `04_click_phrases/`, started from the pico-examples
  `usb/device/dev_hid_composite` example (which is the TinyUSB
  `hid_composite` example), trimmed to a single keyboard interface. The
  ASCII-to-keycode table used by image 4 is TinyUSB's `HID_ASCII_TO_KEYCODE`.

```
The MIT License (MIT)

Copyright (c) 2019 Ha Thach (tinyusb.org)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

The pico-sdk and TinyUSB themselves are not vendored; they are fetched
separately at build time via `PICO_SDK_PATH`.
