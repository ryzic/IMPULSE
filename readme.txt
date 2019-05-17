1) First before we recompile kernel, let’s check if the system is up to date. Custom KELNER  tested with Linux raspberrypi 4.14.98-v7+

~ $ sudo apt-get update
~ $ sudo reboot

2) Let’s setup builder folder in home file
~ $ mkdir rpi
~ $ cd rpi

3) Get missing decencies
~ /rpi $ sudo apt-get install bc

4) Check if you have instaledl git:
~ $ sudo apt-get install git

5) For retrieving the Linux source get the rpi-source script:
~/rpi $ sudo wget https://raw.githubusercontent.com/notro/rpi-source/master/rpi-source -O /usr/bin/rpi-source && sudo chmod +x /usr/bin/rpi-source && /usr/bin/rpi-source -q --tag-update

6) Run the script to download the Linux source that matches the installed version of Linux on your RPi:
~/rpi $ rpi-source -d ./ --nomake –delete

7) On the RPi,
~/rpi $ cd linux
~/rpi/linux $ KERNEL=kernel7
~/rpi/linux $ make bcm2709_defconfig

8) Recompile kelner
 ~/rpi/linux $ make -j4 zImage
9) Download from https://github.com/ryzic/IMPULSE.git

How tu run pulse-generator 

/rpi/utils/pulse-generator/driver	# Compile the pulse-generator driver
$ sudo make
$ sudo make cd ..

/rpi/utils/pulse-generator
$ sudo make				# Compile the cript
$ sudo ./pulse-generator load-driver 4 	# Driver will be loaded to kernel and pin 4 is set up for pulse
$ sudo ./pulse-generator		# Run the script
$ sudo make clean			# Cleans files and unload driver from kernel