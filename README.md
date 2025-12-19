# Immersun T1070 Emulation
Softwre supply of power use for T1060
I have an Immersun power diverter with Ilink and the CT wireless link.
I already have multiple CTs attached to my power lin and there was little room for more. So I decided to emulate the CTs wireless data sender.
ImmerSun refer to this device as a Wireless Sensor T1070
I did contact ImmerSun to try and get my devices accepted so I could use their app, but they ignored me so I had to do this.
The Diverter is the main central device and controls the comms between the various modules. The devices I have use the RF69 Transceiver
This transmits on 869.2MHz (Channel 1) the data is sent at 38,400bps using FSK.
The format I discovered using URH it has the following
3 Preamble Bytes of 0xAA
4 Sync bytes 0x69, 0x81, 0x7E, 0x96
5 Header bytes LL, TT, FF, ID, FL
where LL = length of payload
      TT =  To device
      FF = From device
      ID = (This device ID)
      FL = Flags (as per RF95 data sheet)

However Immersun do not use this addressing method but do use it as another filter so these bytes have to match sending device in my case 
it was LL 00 F5 4E FF (obviously the LL value depends on payload length)

Captured data looks like this
aaaaaa869817e960b00f54effd0ff000102010037b2aa


                  ¦       Payload         ¦
¦ preamble ¦ sync bytes ¦ length¦ header ¦ useable data ¦ crc ¦
   aaaaaa     869817e96   0b     00f54eff  d0ff0001020100 37b2

   Finally had a day of sun so I was able to test it fully I am pleased to be able to say it works perfectly, as well as the official
   CT link T1070. I have added the ability to stop diverting via the Mqtt network. In my case this is automatically controlled by 
   Home Assistant when the car is plugged in it uses the excess to charge the EV. All in all a successful project.
   I used an ESP32-WROOM-32D with a RFM69 module connected to the SPI bus
   

Demodulated data looks like this
D0 FF 00 01 02 01 00
