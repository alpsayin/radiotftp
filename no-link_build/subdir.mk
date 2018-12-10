################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../ax25.c \
../datacollection.c \
../devtag-allinone.c \
../lock.c \
../manchester.c \
../printAsciiHex.c \
../radiotftp.c \
../tftp.c \
../timers.c \
../udp_ip.c \
../util.c 

OBJS += \
./ax25.o \
./datacollection.o \
./devtag-allinone.o \
./lock.o \
./manchester.o \
./printAsciiHex.o \
./radiotftp.o \
./tftp.o \
./timers.o \
./udp_ip.o \
./util.o 

C_DEPS += \
./ax25.d \
./datacollection.d \
./devtag-allinone.d \
./lock.d \
./manchester.d \
./printAsciiHex.d \
./radiotftp.d \
./tftp.d \
./timers.d \
./udp_ip.d \
./util.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -I"../Headers" -O3 -g3 -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


