# STM32F303 ADC temperature display (8-bit signed integer in two's complement)

A bare-metal example for the STM32F3DISCOVERY board (STM32F303VCT6). It reads
ADC1 channel 16, applies a moving average, converts the internal temperature
sensor reading to degrees Celsius, and displays the result on the eight onboard
LEDs. The code uses direct register access, without HAL or an RTOS.

The sensor measures the **MCU die temperature**, which can differ from room
temperature. The included reference calibration comes from one board and should
be adjusted for your own setup.

## Requirements

- STM32F3DISCOVERY with its onboard ST-LINK and a data-capable Mini-B USB cable.
- GNU Arm Embedded toolchain (`arm-none-eabi-gcc`) and GNU Make.
- OpenOCD for flashing and debugging; an Arm-capable GDB for debugging.

On Fedora, for example:

```sh
sudo dnf install arm-none-eabi-gcc make openocd gdb usbutils
```

Package names and GDB's Arm support vary by distribution. Connect the cable to
the board's ST-LINK USB connector and ensure the ST-LINK jumpers are configured
for the onboard target. If OpenOCD cannot access the device, check the USB
connection and your system's ST-LINK udev permissions.

## Build and flash

From the repository directory:

```sh
make build       # Build blink.elf without accessing the board
make flash       # Build if needed, then program, verify, and reset the board
```

Flashing replaces the firmware currently on the board. `make run` is an alias
for `make flash`. **Plain `make` cleans, builds, and flashes**, so use
`make build` when only a build is wanted. `make clean` removes the generated
ELF files. The OpenOCD command uses the included `stm32f3.cfg`.

## Reading the LEDs

The display is an 8-bit signed integer in two's complement, representing whole
degrees Celsius. Values are clamped to -128 through 127 °C. The bit-to-pin
mapping in `led_pins` is:

| Bit | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| GPIOE pin | PE12 | PE11 | PE10 | PE9 | PE8 | PE15 | PE14 | PE13 |
| Weight | -128 | 64 | 32 | 16 | 8 | 4 | 2 | 1 |

For example, 21 °C is `00010101`: bits 4, 2, and 0 are on (PE9, PE15,
and PE13). The display updates roughly every 100 ms.

## Filtering and calibration

`ADC_FilterSetPoints(8)` in `main()` selects an eight-reading moving average
before temperature conversion. The window can be set from 1 to 64 readings;
the function returns zero for an invalid size and leaves the previous setting
unchanged. Changing the size resets the history. Until the window fills, the
average uses only the readings collected so far. At roughly 100 ms per reading,
eight readings span about 700 ms.

`ADC_TemperatureC()` uses the slope from the MCU's factory calibration readings
at 30 °C and 110 °C, together with a user-supplied reference point. In `main.c`:

- `TEMP_REFERENCE_ADC` is the measured ADC reading at the reference point.
- `TEMP_REFERENCE_C` is the known temperature at that point in whole °C.
- `ADC_VDDA_MV` is the ADC supply/reference voltage in millivolts; its default
  is 3300 mV. Measure VDDA and update this value if necessary.

The checked-in reference is ADC 1932 at 21 °C, from one debugging session. To
calibrate another board, allow the setup to stabilize at a known **die**
temperature, observe the filtered ADC reading (for example with GDB), and set
both reference constants. Take the reference reading at the same VDDA used in
normal operation. A single reference point corrects the offset; it does not
establish accuracy over the full temperature range. If the two factory values
are equal, the function returns 0 °C to avoid division by zero.

Factory calibration details are in the [STM32F303xB/xC datasheet](https://www.st.com/resource/en/datasheet/stm32f303cc.pdf).

## Debugging

Build and flash the debug image, then start OpenOCD:

```sh
make debug
openocd -f stm32f3.cfg -c "program blink-debug.elf verify reset exit"
openocd -f stm32f3.cfg
```

In a second terminal, start an Arm-capable GDB (the executable may have a
different name on your system):

```sh
arm-none-eabi-gdb blink-debug.elf
```

For example, inspect the filtered ADC value passed into the conversion:

```gdb
target extended-remote localhost:3333
monitor reset halt
break ADC_TemperatureC
continue
print adc
```

Stop the running OpenOCD server before using `make flash` or another OpenOCD
programming command.

## Project files

- `main.c` — ADC, filter, temperature conversion, and LED display.
- `startup.c` and `linker.ld` — minimal startup and memory layout.
- `Makefile` and `stm32f3.cfg` — build and OpenOCD configuration.

The minimal startup code does not initialize `.data` or clear `.bss`, and only
provides reset vectors. The current firmware explicitly initializes the filter
state before use. Extend the startup code and vector table before relying on
initialized global variables, zero-initialized state, or interrupts.

## License

The project is distributed under the [MIT License](LICENSE). The included
`stm32f3.cfg` carries its own `GPL-2.0-or-later` SPDX identifier.
