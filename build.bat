@echo off
rm elec*
particle compile electron .
FOR %%F IN (*.bin) DO (
 set filename=%%F
 goto tests
)
:tests
echo "%filename%"
particle flash --usb %filename%
