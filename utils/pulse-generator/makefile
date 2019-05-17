# All of the sources participating in the build are defined here
-include subdir.mk

# All Target
all: pulse-generator

# Tool invocations
pulse-generator: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: G++ Linker'
	g++ -static -o "pulse-generator" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	sudo cp ./driver/pulse-generator.ko /lib/modules/4.14.98-v7+/kernel/drivers/misc/pulse-generator.ko
	@echo ' '

# Other Targets
clean:
	-$(RM) $(OBJS) $(CPP_DEPS) $(EXECUTABLES) pulse-generator
	sudo rm /lib/modules/4.14.98-v7+/kernel/drivers/misc/pulse-generator.ko
	-@echo ' '
