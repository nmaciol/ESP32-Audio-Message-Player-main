@echo off
echo Starte Flash-Vorgang...

python -m esptool --chip esp32 --port COM4 --baud 921600 write_flash -z ^
0x1000 bootloader.bin ^
0x8000 partitions.bin ^
0x10000 firmware.bin

echo Fertig!
pause