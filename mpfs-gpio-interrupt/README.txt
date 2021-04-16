================================================================================
                    PolarFire SoC MSS GPIO example
================================================================================

This example project demonstrates the use of the PolarFire SoC MSS GPIO block. 
Both the input and output port configurations are demonstrated along with the 
interrupt handling for the input ports.

--------------------------------------------------------------------------------
                            How to use this example
--------------------------------------------------------------------------------
To use this project you will need a UART terminal configured as below:
    - 115200 baud
    - 8 data bits
    - 1 stop bit
    - no parity
    - no flow control

The example project will display instructions over the serial port. A greeting
message and menu instructions are displayed over the UART terminal. Follow the
instruction to use different menu options provided by the example project.

--------------------------------------------------------------------------------
                                Target hardware
--------------------------------------------------------------------------------
This example project can be used on the PolarFire SoC model on Renode emulation 
platform as well as PolarFire SoC FPGA family hardware platforms. 

There are configurations that need to be set for this example project. The
configurations are categorized into hardware and software configurations.
The hardware configurations are located in ./src/boards/<target_board> folder.
The default software configurations are stored under 
.src/platform/platform_config_reference folder.

The include files in the "./src/boards/<target_board>/fpga_config" folder define 
the hardware configurations such as clocks. You must make sure that the 
configurations in this example project match the actual configurations of your 
target Libero design that you are using to test this example project.

If you need to change the software configurations, you are advised to create a 
new folder to replicate this folder under the ./src/boards directory and do the 
modifications there. It would look like 
./src/boards/<target_board>/platform_config

The include files in the "platform_config" folder define the software 
configurations such as how many harts are being used in the software, what is 
the tick rate of the internal timer of each hart. These configurations have no 
dependency on the hardware configurations in "fpga_config" folder. Note that 
changing these software configurations may require a change in your application 
code.

## Executing project in the Renode emulation

To launch the PolarFire Soc Icicle board emulation model on Renode from 
SoftConsole, launch the preconfigured external tool from
   Run -> External Tools -> "PolarFire-SoC-Icicle-Renode-emulation-platform". 

This will also launch an UART terminal which works with Renode.

Build the project and launch the debug configuration named 
"mpfs-gpio-interrupt renode all-harts debug.launch" which is configured for
Renode.

Alternatively, you can perform above two steps in one go by launching 
mpfs-gpio-interrupt renode all-harts start-platform-and-debug.launch which
will launch the PolarFire Soc Icicle board emulation model on Renode and also
launch a debug session with it.

Further information on working with Renode is available from the "Renode 
emulation platform" section of the SoftConsole release notes. The SoftConsole
release notes document can be found at: <SoftConsole-install-dir>/documentation

## Executing project on the PolarFire SoC hardware

This application can be used on PolarFire hardware platform as well e.g. Icicle 
Kit. In this case, the MMUART0 must be connected to a host PC. The host PC must 
connect to the serial port using a terminal emulator such as Tera Term or PuTTY 
or the SoftConsole built-in terminal view.

Build the project and launch the debug configuration named 
mpfs-gpio-interrupt hw all-harts debug.launch which is configured for 
PolarFire SoC hardware platform.