# ECE 480 Design Team 2 - Low-cost inkjet printer

This program generates a series of commands sent to an Arduino to control stepper motors on a 3D printer frame and a HP6602A cartridge.

**Note:** This project is designed to run on Linux. If you're using Windows, use WSL and [usbipd-win](https://github.com/dorssel/usbipd-win)

## Installation and usage
### Installation
```
git clone https://github.com/tufourn/ece480.git
cd ece480
make
```
### Usage
```
Usage: ./main [OPTION]

  -i <path>    Specify the path to the image file to be printed
  -t           Test nozzles
  -h           Home the printhead
```

You may need to change the permission of the serial port for it to work
```
sudo chmod a+rw /dev/ttyACM0
```
Or add yourself to the `dialout` group (`uucp` group if on Arch)
```
sudo usermod -a -G dialout $USER
```

#### WSL instructions
After installing usbipd, open `cmd` in administrator mode, and a WSL terminal, then run
```bash
usbipd list
```

Find the bus ID of the arduino, then run
```bash
usbipd bind --bus-id <bus_id>
```

Attach the device to WSL
```bash
usbipd attach --busid <bus_id>
```

Check to make sure the device shows up in WSL
```bash
lsusb
```

## How it works
This program uses [stb_image](https://github.com/nothings/stb/blob/master/stb_image.h) to read an image, calculate pixel luminance values, and generates a series of commands that are sent to an Arduino over serial.
- `D`: Changes state of Arduino to dispensing state. The next 2 bytes are intepreted as bitmasks for the nozzles. The LSB of the bitmask controls the nozzle closer to origin. After the nozzles finish dispensing ink, the cartridge is moved to the right by 1 `dotWidth`.
- `R`: Moves cartridge to the beginning of the current line.
- `N`: Moves cartridge up by one line `(nozzleCount * dotHeight)`.
- `T`: Dispense all 12 nozzles for testing
- `H`: Home
