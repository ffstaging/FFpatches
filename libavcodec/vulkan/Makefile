GEN_CLEANSUFFIXES = *.o *.c *.d

clean::
	$(RM) $(GEN_CLEANSUFFIXES:%=libavcodec/vulkan/%)

OBJS-$(CONFIG_FFV1_VULKAN_ENCODER)  +=  vulkan/vulkan_source.o

OBJS-$(CONFIG_FFV1_VULKAN_HWACCEL)  +=  vulkan/vulkan_source.o

OBJS-$(CONFIG_PRORES_RAW_VULKAN_HWACCEL) += vulkan/vulkan_source.o

VULKAN = $(subst $(SRC_PATH)/,,$(wildcard $(SRC_PATH)/libavcodec/vulkan/*.comp))
.SECONDARY: $(VULKAN:.comp=.c)
libavcodec/vulkan/%.c: TAG = VULKAN
libavcodec/vulkan/%.c: $(SRC_PATH)/libavcodec/vulkan/%.comp
	$(M)$(SRC_PATH)/tools/source2c $< $@
