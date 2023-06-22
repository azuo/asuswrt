# Flash for the first time
1. **WARNING**: Double check your router's spec: Tenda AC9 (Broadcom, 16M flash, 64M RAM, no USB). A mismatched router will be bricked.
2. Download the firmware file (*.trx).
3. Connect your computer to one of the router's LAN ports and run `ping 192.168.0.1` (or `ping -t 192.168.0.1` on Windows).
5. Unplug the router, and plug it back in while holding down the reset button. Wait about 30 seconds until `ping` success, then release the reset button.
6. Open your browser and navigate to `http://192.168.0.1`.  Clear NVRAM first, then upload the firmware.
7. Wait patiently for the router to reboot. Done!

# Upgrade
1. Download the firmware file (*.trx).
2. Open the router's web management interface, navigate to *Administration - Firmware Upgrade*.
3. Upload and done.
