################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../ax25.c \
../devtag-allinone.c \
../ethernet.c \
../lock.c \
../manchester.c \
../radiotftp.c \
../tftp.c \
../timers.c \
../udp_ip.c \
../util.c 

OBJS += \
./ax25.o \
./devtag-allinone.o \
./ethernet.o \
./lock.o \
./manchester.o \
./radiotftp.o \
./tftp.o \
./timers.o \
./udp_ip.o \
./util.o 

C_DEPS += \
./ax25.d \
./devtag-allinone.d \
./ethernet.d \
./lock.d \
./manchester.d \
./radiotftp.d \
./tftp.d \
./timers.d \
./udp_ip.d \
./util.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I"/home/alpsayin/wsn-longrange-radio-uplink/project_radiotftp/Header Files" -O3 -g3 -pg -c -fmessage-length=0 -march=i586 -static -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


