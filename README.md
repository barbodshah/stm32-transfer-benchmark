# STM32 transfer benchmark

An embedded C coursework benchmark for the STM32F103C8T6 (Arm Cortex-M3). It tests 256, 1024, 2048, and 4096-byte transfers. The DWT cycle counter measures elapsed time, which the firmware reports in integer microseconds using the configured HCLK.

| Transfer | Methods |
| --- | --- |
| RAM to USART2 | DMA, polled programmed I/O, interrupt-driven UART |
| RAM to RAM | DMA, CPU copy loop |

## Build and run

1. Import this folder as an existing project into STM32CubeIDE. Build the `Q33` project using the bundled HAL/CMSIS source and flash it over SWD.
2. Connect a 3.3 V USB-to-UART adapter's RX to PA9 (USART1 TX), with a shared ground. Open a terminal at 115200 baud, 8 data bits, no parity, and 1 stop bit.
3. Reset the MCU to run the benchmark and print the timing tables. USART2 TX on PA2 carries the benchmark payload. A second adapter or logic analyzer is optional.

USART timings include wire transmission, while RAM timings measure the copy path. The DMA and interrupt paths also count background loop iterations internally. The printed tables report timing only. Keep compiler optimization settings consistent when comparing runs, especially for the C copy loop.

`Core/Src/main.c` contains the benchmark. `Q33.ioc` is the archived CubeMX configuration. Build the checked-in sources directly because regenerating from the configuration may overwrite handwritten initialization. The bundled STM32Cube driver and CMSIS license files are under `Drivers/`.
