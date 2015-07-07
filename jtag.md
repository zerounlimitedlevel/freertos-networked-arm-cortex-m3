Well, you are going to need a JTAG...

# Introduction #

There are many JTAG devices on the market.  Some you may want to stay away from as they have a vendor lock to here dev apps.  READ READ READ!

An open JTAG is what I have chosen.  Search EBay for "open jtag"
Mine was about $50 with shipping.
> I am using this one http://freertos-networked-arm-cortex-m3.googlecode.com/files/jtagpic_fromebay.JPG which was described as

USB Open JTAG Emulator ARM7 ARM9 Cortex-M3 XScale DB9

The rest of this page will describe setting up this device.  I will later add a page on using this device.


# Installing Your JTAG #

  1. download and install the udev config file as /etc/udev/rules.d/50-ftdi.rules
    * http://freertos-networked-arm-cortex-m3.googlecode.com/files/50-ftdi.rules
    * once this is done you should be able to plug the jtag in and linux will see it.
  1. download the openocd configuration file
    * http://freertos-networked-arm-cortex-m3.googlecode.com/files/openocd_lm3s8962.cfg