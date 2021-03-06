# Spieed-k210-c-application

These applications are for Kendryte K210 Sipeed Maix Dock

# Requirement

Note：Only useful for Sipeed Maix Dock

# Usage

## Build from Code

- Linux 

 Enter directory and build it.

```bash
cd spieed-k210-c-application
mkdir build && cd build
cmake .. -DPROJ=dvp_lcd -DTOOLCHAIN=/opt/riscv-toolchain/bin && make
cmake .. -DPROJ=face_mask -DTOOLCHAIN=/opt/riscv-toolchain/bin && make
cmake .. -DPROJ=fire_detect -DTOOLCHAIN=/opt/riscv-toolchain/bin && make
cmake .. -DPROJ=fire_detect -DTOOLCHAIN=/opt/riscv-toolchain/bin && make
```

You will get 8 key files, `xxxx` and `xxxx.bin`.

## Use the Already compiled

All application had itself build file in the `build` folder

1. If you are using JLink to run or debug your program, use `xxxx`
2. If you want to flash it in UOG, using `xxxx.bin`, then using flash-tool(s) burn `xxxx.bin` to your flash.

This is very important, don't make a mistake in files.

# Results

Uploading 



## Other

if you have any questions,  please be free to contact me.

My mail: junwide@outloook.com